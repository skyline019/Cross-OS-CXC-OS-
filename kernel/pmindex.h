// High-speed index: Aurora-inspired prefix + segment invert +
// key quantization + binary postings (thesis/pmem-fs).
#ifndef XV6_PMINDEX_H
#define XV6_PMINDEX_H

#include "types.h"

enum {
  PM_QBITS       = 48,          // bucket = key >> QBITS (16-bit bucket space)
  PM_NBUCKETS    = 1u << 16,
  PM_IDX_KEYS    = 512,         // sorted key table capacity
  PM_POST_RUNS   = 48,          // must match PM_MAX_RUNS
  PM_POST_WORDS  = (PM_POST_RUNS + 63) / 64,
  PM_QCACHE      = 32,
  PM_BUCKET_HIT  = 16,          // max (rid) hints per bucket scan
};

// Binary posting: which runs may contain keys in this quantized bucket.
struct pm_posting {
  uint64 bits[PM_POST_WORDS];
};

struct pm_idx_ent {
  uint64 key;
  uint32 rid;
  uint32 page;   // page index within run
};

struct pm_qcent {
  uint8  used;
  uint64 key;
  uint32 len;
  uint8  val[64];
  uint8  found;  // 1=hit, 0=negative cache
};

struct pm_index {
  uint32 nkeys;
  struct pm_idx_ent keys[PM_IDX_KEYS];  // sorted by key
  // Sparse: only touch buckets that appear (list of used bucket ids).
  uint32 nb_used;
  uint16 used_b[256];
  struct pm_posting post[256];  // parallel to used_b (compact), OR dense map
  // Dense posting by bucket high 8 bits of quantized id for simplicity:
  // full 64k is too big for kernel static — use 256 coarse bins + refine.
  struct pm_posting coarse[256];  // key>>(QBITS+8) → run bits
  struct pm_qcent cache[PM_QCACHE];
  uint32 cache_i;
  uint64 qhits, qmiss, buck_skip, prefix_hits;
};

void     pmindex_init(struct pm_index *ix);
void     pmindex_clear(struct pm_index *ix);

// Quantization / binarization
uint32   pm_key_bucket(uint64 key);          // fine 16-bit
uint32   pm_key_coarse(uint64 key);          // 8-bit coarse
void     pm_post_set(struct pm_posting *p, uint32 rid);
int      pm_post_test(const struct pm_posting *p, uint32 rid);

// Index updates (call on seal for each key)
int      pmindex_upsert(struct pm_index *ix, uint64 key, uint32 rid, uint32 page);

// Lookups
int      pmindex_lower_bound(struct pm_index *ix, uint64 key); // index in keys[] or -1
int      pmindex_hint_runs(struct pm_index *ix, uint64 key, uint32 *out_rids, uint32 maxn);
int      pmindex_cache_get(struct pm_index *ix, uint64 key, void *dst, uint32 dstcap, uint32 *len, int *found);
void     pmindex_cache_put(struct pm_index *ix, uint64 key, const void *val, uint32 len, int found);
void     pmindex_cache_invalidate(struct pm_index *ix, uint64 key);

int      pmindex_selftest(void);

#endif
