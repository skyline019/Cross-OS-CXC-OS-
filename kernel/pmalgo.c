// Append-only, checksummed KV runs. A checkpoint names the newest run; each
// run points to the previous run. Unreachable pages are never reused while a
// checkpoint may reference them. Compaction/reclamation is intentionally off.
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmalgo.h"
#include "pmindex.h"
#include "pmem.h"
#include "pmcontract.h"

#define PMRUN_MAGIC     0x33554e52534d5055ULL
#define PMRUN_VERSION   1u
#define PMRUN_MAX_PAGES 8
#define PM_CHUNK_PREFIX 0xc000000000000000ULL
#define PM_CHUNK_MASK   0xff00000000000000ULL
#define PM_CHUNK_ID_MAX 0x00ffffffffffffffULL

struct pmrun_page {
  uint64 magic;
  uint64 seq;
  uint32 version;
  uint32 page0;
  uint32 page_index;
  uint32 npages;
  uint32 previous;
  uint32 nrecords;
  uint32 checksum;
  uint32 reserved;
};

typedef char
  pmrun_header_fits[(sizeof(struct pmrun_page) < PMDEV_PAGE) ? 1 : -1];
static struct pm_algo g_algo;
static uint8 run_buffer[PMRUN_MAX_PAGES][PMDEV_PAGE];
static uint8 read_buffer[PMDEV_PAGE];

static uint64
mix64(uint64 x)
{
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

static void
bloom_add(struct pm_bloom *b, uint64 key)
{
  uint64 h = mix64(key);
  uint32 i;
  for (i = 0; i < 3; i++) {
    uint32 bit = (uint32)(h >> (i * 16)) & 255u;
    b->w[bit >> 6] |= 1ULL << (bit & 63u);
    h = mix64(h ^ (0x9E3779B97F4A7C15ULL + i));
  }
}

static int
bloom_maybe(const struct pm_bloom *b, uint64 key)
{
  uint64 h = mix64(key);
  uint32 i;
  for (i = 0; i < 3; i++) {
    uint32 bit = (uint32)(h >> (i * 16)) & 255u;
    if ((b->w[bit >> 6] & (1ULL << (bit & 63u))) == 0)
      return 0;
    h = mix64(h ^ (0x9E3779B97F4A7C15ULL + i));
  }
  return 1;
}

static uint32
page_checksum(uint8 *page)
{
  struct pmrun_page *h = (struct pmrun_page *)page;
  uint32 saved = h->checksum, sum = 2166136261u, i;
  h->checksum = 0;
  for (i = 0; i < PMDEV_PAGE; i++) {
    sum ^= page[i];
    sum *= 16777619u;
  }
  h->checksum = saved;
  return sum;
}

static int
read_run_page(struct pm_algo *a, uint32 pid, uint8 *page)
{
  struct pmrun_page *h = (struct pmrun_page *)page;
  if (pid < PMFS_DATA0 || pid >= a->fs->tail ||
      pmdev_read(&a->fs->dev, pid, 0, page, PMDEV_PAGE) != 0)
    return -1;
  if (h->magic != PMRUN_MAGIC || h->version != PMRUN_VERSION ||
      h->checksum != page_checksum(page))
    return -1;
  return 0;
}

struct pm_algo *
pm_algo_global(void)
{
  return &g_algo;
}

void
pm_algo_init(struct pm_algo *a, struct pmfs *fs)
{
  memset(a, 0, sizeof(*a));
  a->fs = fs;
  a->next_seq = 1;
  a->next_chunk = 1;
  a->wa.budget = 64;
  pmindex_init(&a->ix);
}

int
pm_algo_is_internal_key(uint64 key)
{
  return (key & PM_CHUNK_MASK) == PM_CHUNK_PREFIX;
}

uint64
pm_algo_new_chunk_key(struct pm_algo *a)
{
  uint64 n;
  if (a == 0 || a->next_chunk == 0 || a->next_chunk > PM_CHUNK_ID_MAX)
    return 0;
  n = a->next_chunk++;
  return PM_CHUNK_PREFIX | n;
}

int
pm_put(struct pm_algo *a, uint64 key, const void *val, uint32 len)
{
  uint32 i;
  if (a == 0 || len > PM_VAL_MAX || (len && val == 0))
    return -1;
  for (i = 0; i < a->mem_n; i++) {
    if (a->mem_key[i] == key) {
      a->mem_len[i] = len;
      memset(a->mem_val[i], 0, PM_VAL_MAX);
      if (len)
        memmove(a->mem_val[i], (void *)val, len);
      a->fuse_puts++;
      pmindex_cache_invalidate(&a->ix, key);
      return 0;
    }
  }
  if (a->mem_n >= PM_MEM_MAX)
    return -1;
  i = a->mem_n;
  while (i > 0 && a->mem_key[i - 1] > key) {
    a->mem_key[i] = a->mem_key[i - 1];
    a->mem_len[i] = a->mem_len[i - 1];
    memmove(a->mem_val[i], a->mem_val[i - 1], PM_VAL_MAX);
    i--;
  }
  a->mem_key[i] = key;
  a->mem_len[i] = len;
  memset(a->mem_val[i], 0, PM_VAL_MAX);
  if (len)
    memmove(a->mem_val[i], (void *)val, len);
  a->mem_n++;
  a->fuse_puts++;
  pmindex_cache_invalidate(&a->ix, key);
  return 0;
}

// Validate a whole run and reconstruct its in-memory bloom/fences and index.
// Runs are scanned oldest-to-newest so exact-index upserts prefer newest.
static int
load_run(struct pm_algo *a, uint32 pid, uint32 rid, uint64 *seq_out,
         uint32 *previous_out)
{
  struct pm_run *r = &a->runs[rid];
  struct pmrun_page *h = (struct pmrun_page *)read_buffer;
  uint32 pages, prev, p, nall = 0;
  uint64 seq, last = 0;
  int have_last = 0;

  if (read_run_page(a, pid, read_buffer) != 0 || h->page0 != pid ||
      h->page_index != 0 || h->npages == 0 || h->npages > PMRUN_MAX_PAGES ||
      h->npages > a->fs->tail - pid || h->seq == 0 ||
      (h->previous != PMFS_NIL &&
       (h->previous < PMFS_DATA0 || h->previous >= pid)))
    return -1;
  pages = h->npages;
  prev = h->previous;
  seq = h->seq;
  memset(r, 0, sizeof(*r));
  r->used = 1;
  r->page0 = pid;
  r->npages = pages;
  r->seq = seq;
  r->nfences = pages;
  for (p = 0; p < pages; p++) {
    uint32 off = sizeof(*h), i, count;
    if (read_run_page(a, pid + p, read_buffer) != 0 || h->page0 != pid ||
        h->page_index != p || h->npages != pages || h->previous != prev ||
        h->seq != seq)
      return -1;
    count = h->nrecords;
    if (count == 0 || count > PM_MEM_MAX || nall + count > PM_MEM_MAX)
      return -1;
    for (i = 0; i < count; i++) {
      uint64 key;
      uint32 len;
      if (off + PM_REC_HDR > PMDEV_PAGE)
        return -1;
      memmove(&key, read_buffer + off, sizeof(key));
      memmove(&len, read_buffer + off + 8, sizeof(len));
      if (len > PM_VAL_MAX || len > PMDEV_PAGE - off - PM_REC_HDR ||
          (have_last && key <= last))
        return -1;
      if (!have_last)
        r->first = key;
      if (i == 0)
        r->fences[p] = key;
      r->last = key;
      last = key;
      have_last = 1;
      bloom_add(&r->bloom, key);
      if (pmindex_upsert(&a->ix, key, rid, p) != 0)
        return -1;
      if (pm_algo_is_internal_key(key)) {
        uint64 id = key & PM_CHUNK_ID_MAX;
        if (id >= a->next_chunk)
          a->next_chunk = id + 1;
      }
      r->nbytes += PM_REC_HDR + len;
      r->live_bytes += PM_REC_HDR + len;
      nall++;
      off += PM_REC_HDR + len;
    }
  }
  r->nrec = r->live_recs = nall;
  if (seq_out)
    *seq_out = seq;
  if (previous_out)
    *previous_out = prev;
  return 0;
}

int
pm_algo_recover(struct pm_algo *a)
{
  static uint32 chain[PM_MAX_RUNS];
  uint32 head, n = 0, i;
  uint64 prior = 0;
  struct pmrun_page *h = (struct pmrun_page *)read_buffer;

  if (a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  head = a->fs->run_head;
  while (head != PMFS_NIL) {
    if (n >= PM_MAX_RUNS || read_run_page(a, head, read_buffer) != 0 ||
        h->page0 != head || h->page_index != 0 || h->npages == 0 ||
        h->npages > PMRUN_MAX_PAGES || h->npages > a->fs->tail - head ||
        (h->previous != PMFS_NIL &&
         (h->previous < PMFS_DATA0 || h->previous >= head)))
      return -1;
    chain[n++] = head;
    head = h->previous;
  }
  pmindex_clear(&a->ix);
  a->nruns = 0;
  a->next_chunk = 1;
  for (i = n; i > 0; i--) {
    uint64 seq;
    if (load_run(a, chain[i - 1], a->nruns, &seq, 0) != 0 || seq <= prior)
      return -1;
    prior = seq;
    a->nruns++;
  }
  if (prior == ~(uint64)0)
    return -1;
  a->next_seq = prior + 1;
  return 0;
}

int
pm_algo_rebuild_index(struct pm_algo *a)
{
  uint32 i;
  uint64 prior = 0;
  if (a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  pmindex_clear(&a->ix);
  a->next_chunk = 1;
  for (i = 0; i < a->nruns; i++) {
    uint32 pid = a->runs[i].page0;
    uint64 seq;
    if (load_run(a, pid, i, &seq, 0) != 0 || seq <= prior)
      return -1;
    prior = seq;
  }
  return 0;
}

int
pm_seal(struct pm_algo *a)
{
  uint32 i, p = 0, off = sizeof(struct pmrun_page), np, pid, old_tail, old_head;
  uint32 counts[PMRUN_MAX_PAGES];
  uint64 logical = 0, seq;
  struct pm_run *r;

  if (a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  if (a->mem_n == 0)
    // Refresh the other checkpoint slot without changing any roots.
    return pmfs_commit(a->fs);
  if (a->nruns >= PM_MAX_RUNS || a->next_seq == ~(uint64)0)
    return -1;
  memset(run_buffer, 0, sizeof(run_buffer));
  memset(counts, 0, sizeof(counts));
  for (i = 0; i < a->mem_n; i++) {
    uint32 size = PM_REC_HDR + a->mem_len[i];
    if (off + size > PMDEV_PAGE) {
      if (++p >= PMRUN_MAX_PAGES)
        return -1;
      off = sizeof(struct pmrun_page);
    }
    memmove(run_buffer[p] + off, &a->mem_key[i], sizeof(uint64));
    memmove(run_buffer[p] + off + 8, &a->mem_len[i], sizeof(uint32));
    if (a->mem_len[i])
      memmove(run_buffer[p] + off + PM_REC_HDR, a->mem_val[i], a->mem_len[i]);
    logical += a->mem_len[i];
    counts[p]++;
    off += size;
  }
  np = p + 1;
  old_tail = a->fs->tail;
  old_head = a->fs->run_head;
  if (pmfs_alloc_pages(a->fs, np, &pid) != 0)
    return -1;
  seq = a->next_seq;
  for (p = 0; p < np; p++) {
    struct pmrun_page *h = (struct pmrun_page *)run_buffer[p];
    h->magic = PMRUN_MAGIC;
    h->seq = seq;
    h->version = PMRUN_VERSION;
    h->page0 = pid;
    h->page_index = p;
    h->npages = np;
    h->previous = old_head;
    h->nrecords = counts[p];
    h->checksum = page_checksum(run_buffer[p]);
    if (pmfs_write_data_page(a->fs, pid + p, run_buffer[p]) != 0)
      goto rollback;
  }
  // Only the new checkpoint makes the already-persisted run reachable.
  a->fs->run_head = pid;
  if (pmfs_commit(a->fs) != 0)
    goto rollback;
  r = &a->runs[a->nruns];
  if (load_run(a, pid, a->nruns, 0, 0) != 0) {
    // Media verification failed after publication. Stop serving this mount.
    a->fs->mounted = 0;
    return -1;
  }
  r->ver = a->fs->epoch;
  a->nruns++;
  a->next_seq++;
  a->mem_n = 0;
  a->wa.logical_bytes += logical;
  a->wa.phys_bytes += (uint64)np * PMDEV_PAGE;
  a->fuse_seals++;
  return 0;

rollback:
  a->fs->tail = old_tail;
  a->fs->run_head = old_head;
  return -1;
}

// No unsafe compaction: return success without discarding any live run.
// Capacity exhaustion is reported by pm_seal instead of dropping old views.
int
pm_maintain(struct pm_algo *a)
{
  return a ? 0 : -1;
}

static int
page_find(struct pm_algo *a, uint32 pid, uint64 key, void *dst, uint32 cap,
          uint32 *out_len)
{
  struct pmrun_page *h = (struct pmrun_page *)read_buffer;
  uint32 off = sizeof(*h), i;
  a->wa.page_reads++;
  if (read_run_page(a, pid, read_buffer) != 0)
    return -1;
  for (i = 0; i < h->nrecords; i++) {
    uint64 k;
    uint32 len;
    if (off + PM_REC_HDR > PMDEV_PAGE)
      return -1;
    memmove(&k, read_buffer + off, sizeof(k));
    memmove(&len, read_buffer + off + 8, sizeof(len));
    if (len > PM_VAL_MAX || len > PMDEV_PAGE - off - PM_REC_HDR)
      return -1;
    if (k == key) {
      if (len > cap || (len && dst == 0))
        return -1;
      if (len)
        memmove(dst, read_buffer + off + PM_REC_HDR, len);
      if (out_len)
        *out_len = len;
      return 1;
    }
    if (k > key)
      return 0;
    off += PM_REC_HDR + len;
  }
  return 0;
}

int
pm_get(struct pm_algo *a, uint64 key, void *dst, uint32 cap, uint32 *out_len)
{
  uint32 i, len = 0;
  uint8 cached[64];
  int found = 0;
  if (a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  if (out_len)
    *out_len = 0;
  for (i = 0; i < a->mem_n; i++) {
    if (a->mem_key[i] == key) {
      len = a->mem_len[i];
      if (len > cap || (len && dst == 0))
        return -1;
      if (len)
        memmove(dst, a->mem_val[i], len);
      if (out_len)
        *out_len = len;
      a->fuse_gets++;
      return 1;
    }
  }
  if (pmindex_cache_get(&a->ix, key, cached, sizeof(cached), &len, &found)) {
    if (!found)
      return 0;
    if (len > cap || (len && dst == 0))
      return -1;
    if (len)
      memmove(dst, cached, len);
    if (out_len)
      *out_len = len;
    return 1;
  }
  for (i = a->nruns; i > 0; i--) {
    struct pm_run *r = &a->runs[i - 1];
    uint32 p;
    if (!r->used || key < r->first || key > r->last)
      continue;
    if (!bloom_maybe(&r->bloom, key)) {
      a->wa.bloom_skips++;
      continue;
    }
    for (p = 0; p < r->nfences; p++) {
      int hit;
      if (p + 1 < r->nfences && r->fences[p + 1] <= key) {
        a->wa.fence_skips++;
        continue;
      }
      if (r->fences[p] > key)
        break;
      hit = page_find(a, r->page0 + p, key, dst, cap, &len);
      if (hit < 0)
        return -1;
      if (hit == 1) {
        if (out_len)
          *out_len = len;
        pmindex_cache_put(&a->ix, key, dst, len, 1);
        a->fuse_gets++;
        return 1;
      }
      break;
    }
  }
  pmindex_cache_put(&a->ix, key, 0, 0, 0);
  return 0;
}

struct pm_wa
pm_wa_snapshot(struct pm_algo *a)
{
  struct pm_wa z;
  memset(&z, 0, sizeof(z));
  return a ? a->wa : z;
}

int
pm_algo_selftest(void)
{
  struct pm_algo *a = &g_algo;
  char buf[64];
  uint32 len = 0;
  struct pm_wa w;
  int hit;

  if (a->fs == 0 || !a->fs->mounted) {
    pmcontract_fail("ALGO", 1);
    return -1;
  }

  if (pm_put(a, 1, "apple", 5) != 0) {
    pmcontract_fail("ALGO", 2);
    return -2;
  }
  if (pm_put(a, 2, "banana", 6) != 0) {
    pmcontract_fail("ALGO", 3);
    return -3;
  }
  if (pm_put(a, 9, "pear", 4) != 0) {
    pmcontract_fail("ALGO", 4);
    return -4;
  }
  if (pm_seal(a) != 0) {
    pmcontract_fail("ALGO", 5);
    return -5;
  }

  if (pm_put(a, 1, "peach", 5) != 0) {
    pmcontract_fail("ALGO", 6);
    return -6;
  }
  if (pm_seal(a) != 0) {
    pmcontract_fail("ALGO", 7);
    return -7;
  }

  hit = pm_get(a, 1, buf, sizeof(buf), &len);
  if (hit != 1 || len != 5 || buf[0] != 'p' || buf[1] != 'e') {
    pmcontract_fail("ALGO", 8);
    return -8;
  }

  hit = pm_get(a, 9, buf, sizeof(buf), &len);
  if (hit != 1 || len != 4) {
    pmcontract_fail("ALGO", 9);
    return -9;
  }

  hit = pm_get(a, 7, buf, sizeof(buf), &len);
  if (hit != 0) {
    pmcontract_fail("ALGO", 10);
    return -10;
  }

  w = pm_wa_snapshot(a);
  if (w.logical_bytes == 0 || w.phys_bytes == 0) {
    pmcontract_fail("ALGO", 11);
    return -11;
  }
  if (w.phys_bytes > 16ULL * PMDEV_PAGE) {
    pmcontract_fail("ALGO", 12);
    return -12;
  }

  hit = pm_get(a, 1, buf, sizeof(buf), &len);
  if (hit != 1 || a->ix.qhits < 1) {
    pmcontract_fail("ALGO", 13);
    return -13;
  }

  printk(
    "ONBOX_PMFS_ALGO_PASS logical=%d phys=%d bloom_skip=%d fence_skip=%d splits=%d qhits=%d\n",
    (int)w.logical_bytes, (int)w.phys_bytes, (int)w.bloom_skips,
    (int)w.fence_skips, (int)w.splits, (int)a->ix.qhits);
  pmcontract_mark("ALGO", 1);
  return 0;
}
