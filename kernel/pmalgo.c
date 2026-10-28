// Checksummed KV runs form a newest-to-oldest logical chain. A checkpoint
// names its newest run; allocation protects the graphs of both slots and may
// reuse unreachable pages while sequence numbers retain the logical order.
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmalgo.h"
#include "pmindex.h"
#include "pmem.h"
#include "pmfile.h"
#include "pmstream.h"
#include "pmstats.h"
#include "pmcontract.h"

#define PMRUN_MAGIC           0x33554e52534d5055ULL
#define PMRUN_VERSION_ORDERED 1u // v3 original: previous page must be lower
#define PMRUN_VERSION_REUSED                                                   \
  2u // previous may be anywhere below checkpoint tail
#define PMRUN_MAX_PAGES PMFS_MAX_RUN_PAGES
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
typedef char
  pm_posting_covers_runs[((int)PM_MAX_RUNS <= (int)PM_POST_RUNS) ? 1 : -1];
static struct pm_algo g_algo;
static uint8 run_buffer[PMRUN_MAX_PAGES][PMDEV_PAGE];
static uint8 read_buffer[PMDEV_PAGE];

// Bounded scratch, protected by pmfs_lock. Values remain in immutable source
// pages and are copied only when writing an output run.
#define PM_COMPACT_RECORDS (PM_MAX_RUNS * PM_MEM_MAX)
#define PM_COMPACT_REFS    (PMFILE_MAX_INO * PMFILE_MAX_EXT)
struct compact_record {
  uint64 key;
  uint32 pid;
  uint32 off;
  uint32 len;
  uint32 rid;
};
static struct compact_record compact_rec[PM_COMPACT_RECORDS];
static struct compact_record compact_tmp[PM_COMPACT_RECORDS];
static uint64 compact_keys[PM_COMPACT_REFS];
static uint32 compact_lens[PM_COMPACT_REFS];
static uint8 compact_seen[PM_COMPACT_REFS];
// The entire compacted chain is preallocated before any output is written.
static uint32 compact_sizes[PM_MAX_RUNS];
static uint32 compact_pages[PM_MAX_RUNS];

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

// Version 1 requires a descending physical page chain; version 2 permits
// reclaimed locations. Both still require strict descending sequence numbers
// and non-overlapping pages, checked by pm_algo_trace_runs before recovery.
static int
run_version_ok(uint32 version)
{
  return version == PMRUN_VERSION_ORDERED || version == PMRUN_VERSION_REUSED;
}

static int
run_previous_ok(const struct pmrun_page *h, uint32 tail)
{
  if (h->previous == PMFS_NIL)
    return 1;
  return h->previous >= PMFS_DATA0 && h->previous < tail &&
         h->previous != h->page0 &&
         (h->version != PMRUN_VERSION_ORDERED || h->previous < h->page0);
}

static uint32
run_version(uint32 pid, uint32 previous)
{
  return previous != PMFS_NIL && previous >= pid ? PMRUN_VERSION_REUSED
                                                 : PMRUN_VERSION_ORDERED;
}

static int
read_run_page(struct pm_algo *a, uint32 pid, uint8 *page)
{
  struct pmrun_page *h = (struct pmrun_page *)page;
  if (pid < PMFS_DATA0 || pid >= a->fs->tail ||
      pmdev_read(&a->fs->dev, pid, 0, page, PMDEV_PAGE) != 0)
    return -1;
  if (h->magic != PMRUN_MAGIC || !run_version_ok(h->version) ||
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
  uint32 pages, prev, version, p, nall = 0;
  uint64 seq, last = 0;
  int have_last = 0;

  if (read_run_page(a, pid, read_buffer) != 0 || h->page0 != pid ||
      h->page_index != 0 || h->npages == 0 || h->npages > PMRUN_MAX_PAGES ||
      h->npages > a->fs->tail - pid || h->seq == 0 ||
      !run_previous_ok(h, a->fs->tail))
    return -1;
  pages = h->npages;
  prev = h->previous;
  version = h->version;
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
        h->version != version || h->seq != seq)
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

// Independently trace a checkpoint's chain, including the older slot, before
// any metadata page may be recycled. Validate page headers, checksums, record
// bounds, ordering and non-overlap; never infer the older chain from the
// current in-memory index. Runs are immutable while either slot names them.
int
pm_algo_trace_runs(struct pmfs *fs, uint32 head, uint32 tail, uint64 *bits,
                   uint64 *run_count)
{
  static uint8 page[PMDEV_PAGE];
  struct pmrun_page *h = (struct pmrun_page *)page;
  uint64 newer_seq = ~(uint64)0, runs = 0;

  if (!fs || !bits || tail < PMFS_DATA0 || tail > fs->npages)
    return -1;
  while (head != PMFS_NIL) {
    uint32 pages, prev, version, p, all = 0;
    uint64 seq, last = 0;
    int have_last = 0;
    if (++runs > PM_MAX_RUNS || head < PMFS_DATA0 || head >= tail ||
        pmdev_read(&fs->dev, head, 0, page, PMDEV_PAGE) != 0 ||
        h->magic != PMRUN_MAGIC || !run_version_ok(h->version) ||
        h->checksum != page_checksum(page) || h->page0 != head ||
        h->page_index != 0 || h->npages == 0 || h->npages > PMRUN_MAX_PAGES ||
        h->npages > tail - head || h->seq == 0 || h->seq >= newer_seq ||
        !run_previous_ok(h, tail))
      return -1;
    pages = h->npages;
    prev = h->previous;
    version = h->version;
    seq = h->seq;
    for (p = 0; p < pages; p++) {
      uint32 off = sizeof(*h), i, count, pid = head + p;
      uint64 mask = 1ULL << (pid & 63);
      if ((bits[pid >> 6] & mask) ||
          pmdev_read(&fs->dev, pid, 0, page, PMDEV_PAGE) != 0 ||
          h->magic != PMRUN_MAGIC || h->version != version ||
          h->checksum != page_checksum(page) || h->page0 != head ||
          h->page_index != p || h->npages != pages || h->previous != prev ||
          h->seq != seq)
        return -1;
      bits[pid >> 6] |= mask;
      count = h->nrecords;
      if (count == 0 || count > PM_MEM_MAX || all + count > PM_MEM_MAX)
        return -1;
      for (i = 0; i < count; i++) {
        uint64 key;
        uint32 len;
        if (off + PM_REC_HDR > PMDEV_PAGE)
          return -1;
        memmove(&key, page + off, sizeof(key));
        memmove(&len, page + off + 8, sizeof(len));
        if (len > PM_VAL_MAX || len > PMDEV_PAGE - off - PM_REC_HDR ||
            (have_last && key <= last))
          return -1;
        have_last = 1;
        last = key;
        off += PM_REC_HDR + len;
      }
      all += count;
    }
    newer_seq = seq;
    head = prev;
  }
  if (run_count)
    *run_count += runs;
  return 0;
}

int
pm_algo_recover(struct pm_algo *a)
{
  static uint32 chain[PM_MAX_RUNS];
  static uint64 seen[PMDEV_DIRTY_WORDS];
  uint32 head, n = 0, i;
  uint64 prior = 0;
  struct pmrun_page *h = (struct pmrun_page *)read_buffer;

  if (a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  // Physical order no longer proves acyclicity or page disjointness. Validate
  // every page, sequence and overlap before reconstructing the volatile index.
  memset(seen, 0, sizeof(seen));
  if (pm_algo_trace_runs(a->fs, a->fs->run_head, a->fs->tail, seen, 0) != 0)
    return -1;
  if (a->fs->inode_page != PMFS_NIL &&
      ((seen[a->fs->inode_page >> 6] & (1ULL << (a->fs->inode_page & 63))) ||
       (seen[a->fs->dent_page >> 6] & (1ULL << (a->fs->dent_page & 63)))))
    return -1;
  head = a->fs->run_head;
  while (head != PMFS_NIL) {
    if (n >= PM_MAX_RUNS || read_run_page(a, head, read_buffer) != 0 ||
        h->page0 != head || h->page_index != 0 || h->npages == 0 ||
        h->npages > PMRUN_MAX_PAGES || h->npages > a->fs->tail - head ||
        !run_previous_ok(h, a->fs->tail))
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
  uint32 counts[PMRUN_MAX_PAGES], verified_previous;
  uint64 logical = 0, seq, verified_seq;
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
  if (pmfs_alloc_runs(a->fs, &np, 1, &pid) != 0)
    return -1;
  seq = a->next_seq;
  for (p = 0; p < np; p++) {
    struct pmrun_page *h = (struct pmrun_page *)run_buffer[p];
    h->magic = PMRUN_MAGIC;
    h->seq = seq;
    h->version = run_version(pid, old_head);
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
  if (load_run(a, pid, a->nruns, &verified_seq, &verified_previous) != 0 ||
      verified_seq != seq || verified_previous != old_head) {
    // Media verification failed after publication. Stop serving this mount.
    a->fs->mounted = 0;
    return -1;
  }
  pmfs_gc_note_seal(a->fs, old_head, pid, np);
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

// Sort by key ascending and run recency descending; the first record for a
// key is its newest version. Bottom-up merge sort avoids a kernel-stack
// recursion and remains O(N log N) for the bounded 8192-record input.
static int
compact_before(const struct compact_record *a, const struct compact_record *b)
{
  return a->key < b->key || (a->key == b->key && a->rid >= b->rid);
}

static void
compact_sort(uint32 n)
{
  uint32 width, start;
  for (width = 1; width < n; width *= 2) {
    for (start = 0; start < n; start += 2 * width) {
      uint32 left = start, mid = start + width, end = start + 2 * width;
      uint32 right, dst = start;
      if (mid > n)
        mid = n;
      if (end > n)
        end = n;
      right = mid;
      while (left < mid && right < end) {
        if (compact_before(&compact_rec[left], &compact_rec[right]))
          compact_tmp[dst++] = compact_rec[left++];
        else
          compact_tmp[dst++] = compact_rec[right++];
      }
      while (left < mid)
        compact_tmp[dst++] = compact_rec[left++];
      while (right < end)
        compact_tmp[dst++] = compact_rec[right++];
    }
    memmove(compact_rec, compact_tmp, n * sizeof(compact_rec[0]));
  }
}

// Full-chain COW merge. Output pages are reserved against both checkpoint
// graphs before any writes. Maintenance remains explicit, bounded and requires
// enough disjoint scratch space; it is not an online incremental collector.
int
pm_compact(struct pm_algo *a)
{
  struct pmfs *fs;
  uint32 rid, p, n = 0, keep = 0, out_runs, planned = 0, i;
  uint32 old_tail, old_head, new_head, written = 0, cached_pid = PMFS_NIL;
  uint64 max_internal = 0, saved_next_chunk, seq;
  int refs, allocation;

  if (!a || !(fs = a->fs) || !fs->mounted)
    return PM_COMPACT_ERROR;
  if (a->mem_n || pmfile_any_open() || pmstream_any_active())
    return PM_COMPACT_BUSY;
  if (a->nruns > PM_MAX_RUNS)
    return PM_COMPACT_ERROR;
  refs = pmfile_live_chunks(compact_keys, compact_lens, PM_COMPACT_REFS);
  if (refs < 0)
    return PM_COMPACT_ERROR;
  memset(compact_seen, 0, sizeof(compact_seen));
  for (i = 0; i < (uint32)refs; i++)
    if (!pm_algo_is_internal_key(compact_keys[i]) || compact_lens[i] == 0 ||
        compact_lens[i] > PM_VAL_MAX)
      return PM_COMPACT_ERROR;

  // Collect locations, not values. Every read is checksummed again so corrupt
  // source media cannot silently become a newly published compacted view.
  for (rid = 0; rid < a->nruns; rid++) {
    struct pm_run *r = &a->runs[rid];
    if (!r->used || !r->npages || r->npages > PMRUN_MAX_PAGES)
      return PM_COMPACT_ERROR;
    for (p = 0; p < r->npages; p++) {
      struct pmrun_page *h = (struct pmrun_page *)read_buffer;
      uint32 off = sizeof(*h), j;
      if (read_run_page(a, r->page0 + p, read_buffer) != 0 ||
          h->page0 != r->page0 || h->page_index != p ||
          h->npages != r->npages || h->seq != r->seq || h->nrecords == 0 ||
          h->nrecords > PM_MEM_MAX)
        return PM_COMPACT_ERROR;
      for (j = 0; j < h->nrecords; j++) {
        uint64 key;
        uint32 len;
        if (n >= PM_COMPACT_RECORDS || off + PM_REC_HDR > PMDEV_PAGE)
          return PM_COMPACT_ERROR;
        memmove(&key, read_buffer + off, sizeof(key));
        memmove(&len, read_buffer + off + 8, sizeof(len));
        if (len > PM_VAL_MAX || len > PMDEV_PAGE - off - PM_REC_HDR)
          return PM_COMPACT_ERROR;
        compact_rec[n].key = key;
        compact_rec[n].pid = r->page0 + p;
        compact_rec[n].off = off;
        compact_rec[n].len = len;
        compact_rec[n++].rid = rid;
        if (pm_algo_is_internal_key(key) && key > max_internal)
          max_internal = key;
        off += PM_REC_HDR + len;
      }
    }
  }
  compact_sort(n);
  for (i = 0; i < n;) {
    struct compact_record rec = compact_rec[i];
    uint32 j, next = i + 1;
    int retain = !pm_algo_is_internal_key(rec.key) || rec.key == max_internal;
    while (next < n && compact_rec[next].key == rec.key)
      next++;
    for (j = 0; j < (uint32)refs; j++) {
      if (compact_keys[j] == rec.key) {
        if (compact_lens[j] != rec.len)
          return PM_COMPACT_ERROR;
        compact_seen[j] = 1;
        retain = 1;
      }
    }
    if (retain)
      compact_rec[keep++] = rec;
    i = next;
  }
  for (i = 0; i < (uint32)refs; i++)
    if (!compact_seen[i])
      return PM_COMPACT_ERROR;

  // Keep the greatest historical internal chunk as a high-water marker, even
  // when it is dead, so a restart cannot recycle an older chunk identifier.
  // Count exact output pages before writing anything; insufficient disjoint
  // scratch space leaves the current checkpoint and in-memory index untouched.
  out_runs = (keep + PM_MEM_MAX - 1) / PM_MEM_MAX;
  if (out_runs > PM_MAX_RUNS ||
      (out_runs && (a->next_seq == ~(uint64)0 ||
                    (uint64)out_runs > ~(uint64)0 - a->next_seq)))
    return PM_COMPACT_ERROR;
  for (i = 0; i < keep;) {
    uint32 end = i + PM_MEM_MAX, off = sizeof(struct pmrun_page), np = 1;
    if (end > keep)
      end = keep;
    while (i < end) {
      uint32 size = PM_REC_HDR + compact_rec[i++].len;
      if (off + size > PMDEV_PAGE) {
        np++;
        off = sizeof(struct pmrun_page);
      }
      off += size;
    }
    if (np > PMRUN_MAX_PAGES || planned >= PM_MAX_RUNS)
      return PM_COMPACT_ERROR;
    compact_sizes[planned++] = np;
  }
  if (planned != out_runs)
    return PM_COMPACT_ERROR;
  if (a->nruns == 0 && keep == 0)
    return 0;

  old_tail = fs->tail;
  old_head = fs->run_head;
  // Plan and reserve every output page at once, before changing any media.
  allocation =
    out_runs ? pmfs_alloc_runs(fs, compact_sizes, out_runs, compact_pages) : 0;
  if (allocation != 0)
    return allocation == -2 ? PM_COMPACT_NOSPACE : PM_COMPACT_ERROR;
  saved_next_chunk = a->next_chunk;
  new_head = PMFS_NIL;
  seq = a->next_seq;
  planned = 0;
  for (i = 0; i < keep;) {
    uint32 end = i + PM_MEM_MAX, off = sizeof(struct pmrun_page);
    uint32 counts[PMRUN_MAX_PAGES], np = 1, pid;
    if (end > keep)
      end = keep;
    memset(run_buffer, 0, sizeof(run_buffer));
    memset(counts, 0, sizeof(counts));
    while (i < end) {
      struct compact_record *rec = &compact_rec[i++];
      uint64 key;
      uint32 len, size = PM_REC_HDR + rec->len;
      if (off + size > PMDEV_PAGE) {
        np++;
        off = sizeof(struct pmrun_page);
      }
      // Adjacent sorted records frequently share a source page. Reuse the
      // verified scratch page until the source pid changes.
      if (rec->pid != cached_pid) {
        if (read_run_page(a, rec->pid, read_buffer) != 0)
          goto rollback;
        cached_pid = rec->pid;
      }
      memmove(&key, read_buffer + rec->off, sizeof(key));
      memmove(&len, read_buffer + rec->off + 8, sizeof(len));
      if (key != rec->key || len != rec->len)
        goto rollback;
      memmove(run_buffer[np - 1] + off, &key, sizeof(key));
      memmove(run_buffer[np - 1] + off + 8, &len, sizeof(len));
      if (len)
        memmove(run_buffer[np - 1] + off + PM_REC_HDR,
                read_buffer + rec->off + PM_REC_HDR, len);
      counts[np - 1]++;
      off += size;
    }
    if (planned >= out_runs || np != compact_sizes[planned])
      goto rollback;
    pid = compact_pages[planned++];
    for (p = 0; p < np; p++) {
      struct pmrun_page *h = (struct pmrun_page *)run_buffer[p];
      h->magic = PMRUN_MAGIC;
      h->seq = seq;
      h->version = run_version(pid, new_head);
      h->page0 = pid;
      h->page_index = p;
      h->npages = np;
      h->previous = new_head;
      h->nrecords = counts[p];
      h->checksum = page_checksum(run_buffer[p]);
      if (pmfs_write_data_page(fs, pid + p, run_buffer[p]) != 0)
        goto rollback;
    }
    written += np;
    new_head = pid;
    seq++;
  }
  fs->run_head = new_head;
  if (pmfs_commit(fs) != 0)
    goto rollback;
  // Publication succeeded: rebuild all volatile indexes from the new chain.
  // If verification unexpectedly fails, stop serving rather than roll back
  // an already-durable checkpoint in memory.
  if (pm_algo_recover(a) != 0) {
    fs->mounted = 0;
    return PM_COMPACT_ERROR;
  }
  if (a->next_chunk < saved_next_chunk)
    a->next_chunk = saved_next_chunk;
  a->wa.compacts++;
  a->wa.phys_bytes += (uint64)written * PMDEV_PAGE;
  a->compact_calls++;
  a->compact_input_records += n;
  a->compact_output_records += keep;
  a->compact_output_pages += written;
  return 0;

rollback:
  fs->tail = old_tail;
  fs->run_head = old_head;
  return PM_COMPACT_ERROR;
}

// Automatic or incremental maintenance is intentionally not enabled.
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

// Highest set bit of a nonzero word without a Zbb or compiler-rt dependency.
// Posting words are traversed newest first to preserve latest-value semantics.
static int
highest_bit(uint64 bits)
{
  int n = 0;
  if (bits >> 32) {
    n += 32;
    bits >>= 32;
  }
  if (bits >> 16) {
    n += 16;
    bits >>= 16;
  }
  if (bits >> 8) {
    n += 8;
    bits >>= 8;
  }
  if (bits >> 4) {
    n += 4;
    bits >>= 4;
  }
  if (bits >> 2) {
    n += 2;
    bits >>= 2;
  }
  if (bits >> 1)
    n++;
  return n;
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
  // The bounded exact index is updated oldest-to-newest on recovery and on
  // every successful seal. Even at capacity existing keys are refreshed.
  // It can therefore route a tracked key directly to its newest page; an
  // untracked key must still use the complete fallback below.
  {
    uint32 rid, page;
    if (pmindex_lookup_exact(&a->ix, key, &rid, &page)) {
      int hit;
      if (rid >= a->nruns || !a->runs[rid].used || page >= a->runs[rid].npages)
        return -1;
      hit = page_find(a, a->runs[rid].page0 + page, key, dst, cap, &len);
      if (hit != 1)
        return -1; // inconsistent in-memory index or corrupt page: fail closed
      if (out_len)
        *out_len = len;
      pmindex_cache_put(&a->ix, key, dst, len, 1);
      a->exact_hits++;
      a->fuse_gets++;
      return 1;
    }
  }
  {
    const struct pm_posting *posting = &a->ix.coarse[pm_key_coarse(key)];
    uint32 cursor = a->nruns;
    if (a->nruns > PM_POST_RUNS)
      return -1; // never read beyond the bounded posting bitmap
    // Postings are complete even for keys beyond exact-index capacity. Scan
    // only set bits, newest first, rather than probing every unrelated run.
    for (i = (a->nruns + 63) / 64; i > 0; i--) {
      uint64 bits = posting->bits[i - 1];
      while (bits) {
        uint32 bit = highest_bit(bits), rid = (i - 1) * 64 + bit, p;
        struct pm_run *r;
        bits &= ~(1ULL << bit);
        if (rid >= cursor)
          continue;
        a->posting_skips += cursor - rid - 1;
        cursor = rid;
        a->fallback_run_probes++;
        r = &a->runs[rid];
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
    }
    a->posting_skips += cursor;
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
