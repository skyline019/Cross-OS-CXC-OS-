// Aurora-inspired index: sorted keys + quantized binary postings + query LRU.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmindex.h"

uint32
pm_key_bucket(uint64 key)
{
  return (uint32)(key >> PM_QBITS) & 0xffffu;
}

uint32
pm_key_coarse(uint64 key)
{
  // 8-bit quantization of the 16-bit bucket (Aurora "coarse then fine").
  return (pm_key_bucket(key) >> 8) & 0xffu;
}

void
pm_post_set(struct pm_posting *p, uint32 rid)
{
  if(p == 0 || rid >= PM_POST_RUNS)
    return;
  p->bits[rid >> 6] |= (1ULL << (rid & 63u));
}

int
pm_post_test(const struct pm_posting *p, uint32 rid)
{
  if(p == 0 || rid >= PM_POST_RUNS)
    return 0;
  return (p->bits[rid >> 6] & (1ULL << (rid & 63u))) != 0;
}

void
pmindex_init(struct pm_index *ix)
{
  if(ix == 0)
    return;
  memset(ix, 0, sizeof(*ix));
}

void
pmindex_clear(struct pm_index *ix)
{
  pmindex_init(ix);
}

static int
key_cmp(uint64 a, uint64 b)
{
  if(a < b) return -1;
  if(a > b) return 1;
  return 0;
}

// Binary search lower_bound — stand-in for RbTree.lowerBound / skip-list jump.
int
pmindex_lower_bound(struct pm_index *ix, uint64 key)
{
  int lo, hi, mid;
  if(ix == 0 || ix->nkeys == 0)
    return -1;
  lo = 0;
  hi = (int)ix->nkeys;
  while(lo < hi){
    mid = lo + (hi - lo) / 2;
    if(key_cmp(ix->keys[mid].key, key) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  if(lo >= (int)ix->nkeys)
    return -1;
  return lo;
}

int
pmindex_upsert(struct pm_index *ix, uint64 key, uint32 rid, uint32 page)
{
  int pos, i;
  uint32 c;

  if(ix == 0 || rid >= PM_POST_RUNS)
    return -1;

  // Binary posting at coarse quant bucket (二值化倒排).
  c = pm_key_coarse(key);
  pm_post_set(&ix->coarse[c], rid);

  // Sorted key table (prefix / exact) — like RbTree insert.
  if(ix->nkeys >= PM_IDX_KEYS){
    // Table full: still keep posting bits for filtering.
    return 0;
  }
  pos = pmindex_lower_bound(ix, key);
  if(pos >= 0 && ix->keys[pos].key == key){
    // Newer rid wins (like Aurora upsert).
    ix->keys[pos].rid = rid;
    ix->keys[pos].page = page;
    return 0;
  }
  if(pos < 0)
    pos = (int)ix->nkeys;
  for(i = (int)ix->nkeys; i > pos; i--)
    ix->keys[i] = ix->keys[i - 1];
  ix->keys[pos].key = key;
  ix->keys[pos].rid = rid;
  ix->keys[pos].page = page;
  ix->nkeys++;
  return 0;
}

int
pmindex_hint_runs(struct pm_index *ix, uint64 key, uint32 *out_rids, uint32 maxn)
{
  uint32 c, rid, n = 0;
  const struct pm_posting *p;

  if(ix == 0 || out_rids == 0 || maxn == 0)
    return 0;

  // Exact key in sorted table first (prefix-index style hit).
  {
    int pos = pmindex_lower_bound(ix, key);
    if(pos >= 0 && ix->keys[pos].key == key){
      out_rids[0] = ix->keys[pos].rid;
      ix->prefix_hits++;
      return 1;
    }
  }

  c = pm_key_coarse(key);
  p = &ix->coarse[c];
  for(rid = 0; rid < PM_POST_RUNS && n < maxn; rid++){
    if(pm_post_test(p, rid))
      out_rids[n++] = rid;
  }
  if(n == 0)
    ix->buck_skip++;
  return (int)n;
}

int
pmindex_cache_get(struct pm_index *ix, uint64 key, void *dst, uint32 dstcap,
                  uint32 *len, int *found)
{
  uint32 i;
  if(ix == 0)
    return 0;
  for(i = 0; i < PM_QCACHE; i++){
    struct pm_qcent *e = &ix->cache[i];
    if(e->used && e->key == key){
      ix->qhits++;
      if(found)
        *found = e->found;
      if(e->found){
        if(len)
          *len = e->len;
        if(dst && e->len <= dstcap)
          memmove(dst, e->val, e->len);
      }
      return 1;
    }
  }
  ix->qmiss++;
  return 0;
}

void
pmindex_cache_put(struct pm_index *ix, uint64 key, const void *val, uint32 len,
                  int found)
{
  struct pm_qcent *e;
  if(ix == 0)
    return;
  e = &ix->cache[ix->cache_i % PM_QCACHE];
  ix->cache_i++;
  memset(e, 0, sizeof(*e));
  e->used = 1;
  e->key = key;
  e->found = found ? 1 : 0;
  if(found && val && len <= 64){
    e->len = len;
    memmove(e->val, (void*)val, len);
  }
}

int
pmindex_selftest(void)
{
  struct pm_index ix;
  uint32 rids[8];
  int n, found = 0;
  uint32 len = 0;
  char buf[8];

  pmindex_init(&ix);

  // Quantization: nearby keys share coarse bucket.
  if(pm_key_coarse(0x1000000000000ULL) != pm_key_coarse(0x1000000000001ULL))
    return -1;

  if(pmindex_upsert(&ix, 10, 0, 0) != 0) return -2;
  if(pmindex_upsert(&ix, 20, 1, 0) != 0) return -3;
  if(pmindex_upsert(&ix, 30, 0, 1) != 0) return -4;

  n = pmindex_hint_runs(&ix, 20, rids, 8);
  if(n < 1 || rids[0] != 1) return -5;

  // Binary posting: run0 must be set for keys in same coarse region as 10.
  if(!pm_post_test(&ix.coarse[pm_key_coarse(10)], 0)) return -6;

  pmindex_cache_put(&ix, 20, "ok", 2, 1);
  if(!pmindex_cache_get(&ix, 20, buf, sizeof(buf), &len, &found)) return -7;
  if(!found || len != 2 || buf[0] != 'o') return -8;

  // Negative cache
  pmindex_cache_put(&ix, 99, 0, 0, 0);
  found = 1;
  if(!pmindex_cache_get(&ix, 99, buf, sizeof(buf), &len, &found)) return -9;
  if(found != 0) return -10;

  printk("pmfs: INDEX_SELFTEST_PASS keys=%d qhits=%d buck_skip=%d\n",
         ix.nkeys, (int)ix.qhits, (int)ix.buck_skip);
  return 0;
}
