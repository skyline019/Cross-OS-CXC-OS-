// Persistent-memory window (thesis/pmem-fs).
// Byte-addressable region above PHYSTOP; not given to kalloc.

#ifndef XV6_PMEM_H
#define XV6_PMEM_H

#include "types.h"

enum {
  PMEM_LINE = 64,  // cache-line sized flush quantum (合题：行级刷写)
};

// Physical window (see memlayout.h).
void            pmem_init(void);
int             pmem_ready(void);
uint64          pmem_base_pa(void);
uint64          pmem_size(void);
void*           pmem_base_va(void);   // direct map VA (== PA on xv6 identity map)
const char*     pmem_backend_name(void); // "emul" | "map"

// Make [p, p+n) durable w.r.t. the PMEM persistence domain.
// Protocol: store → pmem_flush (line walk) → pmem_fence → (SB store → flush → fence).
void            pmem_flush(void *p, uint64 n);
void            pmem_fence(void);
void            pmem_persist(void *p, uint64 n);  // flush + fence
uint64          pmem_flush_lines(void);           // cumulative line count
void            pmem_flush_lines_reset(void);

// Development probe: write magic, persist, verify.
int             pmem_selftest(void);
void            pmem_contract_map(void);  // PMEM1 when MAP
int             pmem_persist_selftest(void); // ONBOX_PMFS_PERSIST_*

#endif
