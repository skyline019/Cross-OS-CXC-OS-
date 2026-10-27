// FCS algorithm core on PMFS: WA control, fast get, anti-fragment cells.
// Inspired by e:\FCS\embdb\x\include\fcs.h (Run/Seg/Cell/Bloom/B1–B3) — C port.
#ifndef XV6_PMALGO_H
#define XV6_PMALGO_H

#include "types.h"
#include "pmfs.h"
#include "pmindex.h"

enum {
  PM_BMAX       = 32 * 1024,   // B1: cell physical bytes
  PM_SMAX       = 16,          // B2: compact when segs reach this
  PM_GC_RATIO   = 2,           // compact if dead >= ratio * live
  PM_MAX_RUNS   = 48,
  PM_MAX_CELLS  = 24,
  PM_MAX_SEGS   = 24,          // headroom for multi-N bench sweeps
  PM_MAX_FENCES = 64,
  PM_BLOOM_WORDS = 4,          // 256-bit bloom
  PM_REC_HDR    = 16,          // on-page record: key:u64 len:u32 pad:u32
};

struct pm_bloom {
  uint64 w[PM_BLOOM_WORDS];
};

struct pm_run {
  uint8  used;
  uint32 page0;
  uint32 npages;
  uint64 first, last;
  uint64 seq;          // newer = larger (FCS seq)
  uint64 ver;          // commit epoch when sealed
  uint64 nbytes;       // physical payload bytes in run
  uint64 live_bytes;   // surviving bytes (anti-frag / GC)
  uint32 nrec;
  uint32 live_recs;
  uint32 nfences;
  uint64 fences[PM_MAX_FENCES];  // first key of each page
  struct pm_bloom bloom;
};

// Segment view: cell does NOT own bytes — only (run, page range, key range).
struct pm_seg {
  uint32 rid;
  uint32 p0, p1;       // page indices within run [p0, p1)
  uint64 klo, khi;     // half-open key domain
  uint32 live;
};

struct pm_cell {
  uint8  used;
  uint32 nseg;
  uint64 nbytes;       // sum of covered physical bytes (B1)
  struct pm_seg segs[PM_MAX_SEGS];  // seq-desc order for get
};

struct pm_wa {
  uint64 logical_bytes;  // user payload sealed
  uint64 phys_bytes;     // PAGE * pages written
  uint64 bloom_skips;
  uint64 fence_skips;
  uint64 page_reads;
  uint64 splits;         // B1
  uint64 compacts;       // B2
  uint64 budget;         // B3 rewrite page budget (stub)
};

struct pm_algo {
  struct pmfs *fs;
  uint32 nruns;
  uint32 ncells;
  uint64 next_seq;
  struct pm_run runs[PM_MAX_RUNS];
  struct pm_cell cells[PM_MAX_CELLS];
  struct pm_wa wa;
  struct pm_index ix;   // Aurora: prefix + quant + binary postings + qcache
  // Memtable (single pending sorted batch) — FCS mem → one run / commit.
  uint32 mem_n;
  uint64 mem_key[64];
  uint32 mem_len[64];
  uint8  mem_val[64][64];
};

void pm_algo_init(struct pm_algo *a, struct pmfs *fs);
struct pm_algo *pm_algo_global(void);

// Write path: put into memtable; seal flushes one immutable run + commit.
int  pm_put(struct pm_algo *a, uint64 key, const void *val, uint32 len);
int  pm_seal(struct pm_algo *a);   // mem → run → pmfs_commit; then maintain

// Fast read: cells → segs new-to-old → bloom → fences → page.
int  pm_get(struct pm_algo *a, uint64 key, void *dst, uint32 dstcap, uint32 *out_len);

// Anti-frag: enforce B1/B2; B3 true rewrite under budget.
int  pm_maintain(struct pm_algo *a);

// Rebuild Aurora index from sealed runs (remount / crash recovery).
int  pm_algo_rebuild_index(struct pm_algo *a);

struct pm_wa pm_wa_snapshot(struct pm_algo *a);

// Self-test for algorithm layer.
int  pm_algo_selftest(void);

#endif
