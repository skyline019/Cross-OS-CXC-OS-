// FCS-inspired algorithms on PMEM store (WA / fast read / anti-frag).
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmalgo.h"
#include "pmem.h"

static struct pm_algo g_algo;

static uint64
mix64(uint64 x)
{
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static void
bloom_add(struct pm_bloom *b, uint64 key)
{
  uint64 h = mix64(key);
  uint32 i;
  for(i = 0; i < 3; i++){
    uint32 bit = (uint32)(h >> (i * 16)) & 255u;
    b->w[bit >> 6] |= (1ULL << (bit & 63u));
    h = mix64(h ^ (0x9E3779B97F4A7C15ULL + i));
  }
}

static int
bloom_maybe(struct pm_bloom *b, uint64 key)
{
  uint64 h = mix64(key);
  uint32 i;
  for(i = 0; i < 3; i++){
    uint32 bit = (uint32)(h >> (i * 16)) & 255u;
    if((b->w[bit >> 6] & (1ULL << (bit & 63u))) == 0)
      return 0;
    h = mix64(h ^ (0x9E3779B97F4A7C15ULL + i));
  }
  return 1;
}

static void
rec_encode(uint8 *dst, uint64 key, const void *val, uint32 len)
{
  memset(dst, 0, PM_REC_HDR + len);
  memmove(dst, &key, 8);
  memmove(dst + 8, &len, 4);
  if(len)
    memmove(dst + PM_REC_HDR, (void*)val, len);
}

static int
rec_decode(const uint8 *src, uint64 *key, uint32 *len, const uint8 **val)
{
  memmove(key, (void*)src, 8);
  memmove(len, (void*)(src + 8), 4);
  if(*len > 1024)
    return -1;
  *val = src + PM_REC_HDR;
  return 0;
}

struct pm_algo*
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
  a->wa.budget = 8; // B3: pages allowed to rewrite per maintain (stub)
}

int
pm_put(struct pm_algo *a, uint64 key, const void *val, uint32 len)
{
  uint32 i, j;
  if(a == 0 || len > 64 || a->mem_n >= 64)
    return -1;
  // Upsert in memtable (sorted by key) — FCS put only touches mem.
  for(i = 0; i < a->mem_n; i++){
    if(a->mem_key[i] == key){
      a->mem_len[i] = len;
      memset(a->mem_val[i], 0, 64);
      if(len)
        memmove(a->mem_val[i], (void*)val, len);
      return 0;
    }
  }
  i = a->mem_n;
  while(i > 0 && a->mem_key[i - 1] > key){
    a->mem_key[i] = a->mem_key[i - 1];
    a->mem_len[i] = a->mem_len[i - 1];
    memmove(a->mem_val[i], a->mem_val[i - 1], 64);
    i--;
  }
  a->mem_key[i] = key;
  a->mem_len[i] = len;
  memset(a->mem_val[i], 0, 64);
  if(len)
    memmove(a->mem_val[i], (void*)val, len);
  a->mem_n++;
  (void)j;
  return 0;
}

// Pack memtable into consecutive pages as one immutable run (FCS: 1 run / commit).
static int
seal_run(struct pm_algo *a, uint32 *out_rid)
{
  struct pm_run *r;
  uint32 rid, pid, need, i, off, np;
  uint8 page[PMDEV_PAGE];
  uint64 logical = 0;

  if(a->mem_n == 0)
    return -1;
  if(a->nruns >= PM_MAX_RUNS)
    return -1;

  // Estimate pages: pack records sequentially.
  need = 0;
  for(i = 0; i < a->mem_n; i++)
    need += PM_REC_HDR + a->mem_len[i];
  np = (need + PMDEV_PAGE - 1) / PMDEV_PAGE;
  if(np == 0)
    np = 1;
  if(np > PM_MAX_FENCES)
    return -1;

  if(pmfs_alloc_pages(a->fs, np, &pid) != 0)
    return -1;

  rid = a->nruns;
  r = &a->runs[rid];
  memset(r, 0, sizeof(*r));
  r->used = 1;
  r->page0 = pid;
  r->npages = np;
  r->seq = a->next_seq++;
  r->ver = a->fs->epoch + 1; // will match after commit
  r->first = a->mem_key[0];
  r->last = a->mem_key[a->mem_n - 1];
  r->nrec = a->mem_n;
  r->live_recs = a->mem_n;

  off = 0;
  memset(page, 0, sizeof(page));
  r->nfences = 0;
  r->fences[r->nfences++] = a->mem_key[0];

  for(i = 0; i < a->mem_n; i++){
    uint32 rec = PM_REC_HDR + a->mem_len[i];
    if(off + rec > PMDEV_PAGE){
      if(pmfs_write_data_page(a->fs, pid + (r->nfences - 1), page) != 0)
        return -1;
      a->wa.phys_bytes += PMDEV_PAGE;
      memset(page, 0, sizeof(page));
      off = 0;
      if(r->nfences >= np)
        return -1;
      r->fences[r->nfences++] = a->mem_key[i];
    }
    rec_encode(page + off, a->mem_key[i], a->mem_val[i], a->mem_len[i]);
    bloom_add(&r->bloom, a->mem_key[i]);
    logical += a->mem_len[i];
    r->nbytes += rec;
    r->live_bytes += rec;
    off += rec;
  }
  if(pmfs_write_data_page(a->fs, pid + (r->nfences - 1), page) != 0)
    return -1;
  a->wa.phys_bytes += PMDEV_PAGE;
  a->wa.logical_bytes += logical;
  // If fewer pages used than allocated, still OK (tail already advanced).

  a->nruns++;
  a->mem_n = 0;
  *out_rid = rid;
  return 0;
}

// Attach run as a new segment on a covering cell (single-cell MVP: one global cell).
static int
attach_run(struct pm_algo *a, uint32 rid)
{
  struct pm_cell *c;
  struct pm_run *r = &a->runs[rid];
  struct pm_seg seg;
  uint32 i;

  if(a->ncells == 0){
    c = &a->cells[0];
    memset(c, 0, sizeof(*c));
    c->used = 1;
    a->ncells = 1;
  } else {
    c = &a->cells[0];
  }

  if(c->nseg >= PM_MAX_SEGS)
    return -1; // B2 will compact

  seg.rid = rid;
  seg.p0 = 0;
  seg.p1 = r->npages;
  seg.klo = r->first;
  seg.khi = r->last + 1; // half-open
  seg.live = r->live_recs;

  // Insert at front (newer seq first) — FCS get walks new→old.
  for(i = c->nseg; i > 0; i--)
    c->segs[i] = c->segs[i - 1];
  c->segs[0] = seg;
  c->nseg++;
  c->nbytes += r->nbytes;
  return 0;
}

// B1: if cell too big, split segment list at page boundary — ZERO payload rewrite.
static int
cell_split_b1(struct pm_algo *a, struct pm_cell *c)
{
  struct pm_cell *n;
  uint32 keep, i;

  if(c->nbytes <= PM_BMAX || c->nseg < 2)
    return 0;
  if(a->ncells >= PM_MAX_CELLS)
    return -1;

  keep = c->nseg / 2;
  n = &a->cells[a->ncells++];
  memset(n, 0, sizeof(*n));
  n->used = 1;
  for(i = keep; i < c->nseg; i++){
    n->segs[n->nseg++] = c->segs[i];
    n->nbytes += a->runs[c->segs[i].rid].nbytes;
  }
  // Shrink old cell view only — runs untouched (FCS innovation).
  for(i = keep; i < c->nseg; i++)
    memset(&c->segs[i], 0, sizeof(c->segs[i]));
  c->nseg = keep;
  c->nbytes = 0;
  for(i = 0; i < c->nseg; i++)
    c->nbytes += a->runs[c->segs[i].rid].nbytes;

  a->wa.splits++;
  return 0;
}

// B2: too many segs → mark older segs' bytes as compact candidates (MVP: drop oldest
// from view after copying survivors would be full compact; here we only count).
static int
cell_compact_b2(struct pm_algo *a, struct pm_cell *c)
{
  if(c->nseg <= PM_SMAX)
    return 0;
  // True compact rewrites one run; respect B3 budget.
  if(a->wa.budget == 0)
    return 0;
  // MVP: drop oldest segment from the cell view if its run is fully obsolete
  // (live_bytes==0). Otherwise leave for later full merge.
  {
    struct pm_seg oldest = c->segs[c->nseg - 1];
    struct pm_run *r = &a->runs[oldest.rid];
    if(r->live_bytes == 0 || r->live_recs == 0){
      c->nbytes -= r->nbytes;
      c->nseg--;
      a->wa.compacts++;
      if(a->wa.budget)
        a->wa.budget--;
    }
  }
  return 0;
}

int
pm_maintain(struct pm_algo *a)
{
  uint32 i;
  if(a == 0)
    return -1;
  for(i = 0; i < a->ncells; i++){
    if(!a->cells[i].used)
      continue;
    cell_split_b1(a, &a->cells[i]);
    cell_compact_b2(a, &a->cells[i]);
  }
  return 0;
}

int
pm_seal(struct pm_algo *a)
{
  uint32 rid;
  if(a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  if(a->mem_n == 0)
    return 0;
  if(seal_run(a, &rid) != 0)
    return -1;
  if(attach_run(a, rid) != 0)
    return -1;
  // Data pages persisted; now dual-SB commit (FCS barrier protocol).
  if(pmfs_commit(a->fs) != 0)
    return -1;
  a->runs[rid].ver = a->fs->epoch;
  pm_maintain(a);
  return 0;
}

// Scan one data page for key.
static int
page_find(struct pm_algo *a, uint32 abs_pid, uint64 key,
          void *dst, uint32 dstcap, uint32 *out_len)
{
  uint8 page[PMDEV_PAGE];
  uint32 off = 0;
  a->wa.page_reads++;
  if(pmdev_read(&a->fs->dev, abs_pid, 0, page, PMDEV_PAGE) != 0)
    return -1;
  while(off + PM_REC_HDR <= PMDEV_PAGE){
    uint64 k;
    uint32 len;
    const uint8 *val;
    if(rec_decode(page + off, &k, &len, &val) != 0)
      break;
    if(len == 0 && k == 0 && off > 0)
      break;
    if(off + PM_REC_HDR + len > PMDEV_PAGE)
      break;
    if(k == key){
      if(out_len)
        *out_len = len;
      if(dst && len <= dstcap)
        memmove(dst, (void*)val, len);
      return 1;
    }
    if(k > key)
      return 0;
    off += PM_REC_HDR + len;
  }
  return 0;
}

int
pm_get(struct pm_algo *a, uint64 key, void *dst, uint32 dstcap, uint32 *out_len)
{
  uint32 ci, si, p;
  if(a == 0)
    return -1;

  // Memtable first (FCS: mem wins).
  {
    uint32 i;
    for(i = 0; i < a->mem_n; i++){
      if(a->mem_key[i] == key){
        if(out_len)
          *out_len = a->mem_len[i];
        if(dst && a->mem_len[i] <= dstcap)
          memmove(dst, a->mem_val[i], a->mem_len[i]);
        return 1;
      }
    }
  }

  for(ci = 0; ci < a->ncells; ci++){
    struct pm_cell *c = &a->cells[ci];
    if(!c->used)
      continue;
    for(si = 0; si < c->nseg; si++){
      struct pm_seg *s = &c->segs[si];
      struct pm_run *r = &a->runs[s->rid];
      if(key < s->klo || key >= s->khi)
        continue;
      if(!bloom_maybe(&r->bloom, key)){
        a->wa.bloom_skips++;
        continue;
      }
      // Fence skip: jump to first page whose next fence > key.
      for(p = s->p0; p < s->p1 && p < r->nfences; p++){
        if(p + 1 < r->nfences && r->fences[p + 1] <= key){
          a->wa.fence_skips++;
          continue;
        }
        if(r->fences[p] > key)
          break;
        {
          int hit = page_find(a, r->page0 + p, key, dst, dstcap, out_len);
          if(hit == 1)
            return 1;
          if(hit < 0)
            return -1;
        }
        break; // sorted page: miss → try older seg
      }
    }
  }
  return 0;
}

struct pm_wa
pm_wa_snapshot(struct pm_algo *a)
{
  struct pm_wa z;
  memset(&z, 0, sizeof(z));
  if(a)
    z = a->wa;
  return z;
}

int
pm_algo_selftest(void)
{
  struct pm_algo *a = &g_algo;
  char buf[64];
  uint32 len = 0;
  struct pm_wa w;
  int hit;

  if(a->fs == 0 || !a->fs->mounted)
    return -1;

  if(pm_put(a, 1, "apple", 5) != 0) return -2;
  if(pm_put(a, 2, "banana", 6) != 0) return -3;
  if(pm_put(a, 9, "pear", 4) != 0) return -4;
  if(pm_seal(a) != 0) return -5;

  // Overwrite key 1 without touching old run pages (anti-WA).
  if(pm_put(a, 1, "peach", 5) != 0) return -6;
  if(pm_seal(a) != 0) return -7;

  hit = pm_get(a, 1, buf, sizeof(buf), &len);
  if(hit != 1 || len != 5 || buf[0] != 'p' || buf[1] != 'e') return -8;

  hit = pm_get(a, 9, buf, sizeof(buf), &len);
  if(hit != 1 || len != 4) return -9;

  hit = pm_get(a, 7, buf, sizeof(buf), &len);
  if(hit != 0) return -10;

  w = pm_wa_snapshot(a);
  if(w.logical_bytes == 0 || w.phys_bytes == 0) return -11;
  // phys should be modest: 2 seals → a few pages, not inode-style rewrite storm.
  if(w.phys_bytes > 16ULL * PMDEV_PAGE) return -12;

  printk("pmfs: ALGO_SELFTEST_PASS logical=%d phys=%d bloom_skip=%d fence_skip=%d splits=%d\n",
         (int)w.logical_bytes, (int)w.phys_bytes,
         (int)w.bloom_skips, (int)w.fence_skips, (int)w.splits);
  return 0;
}
