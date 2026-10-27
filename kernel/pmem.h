// Persistent-memory window (thesis/pmem-fs).
// Byte-addressable region above PHYSTOP; not given to kalloc.

#ifndef XV6_PMEM_H
#define XV6_PMEM_H

#include "types.h"

// Physical window (see memlayout.h).
void            pmem_init(void);
int             pmem_ready(void);
uint64          pmem_base_pa(void);
uint64          pmem_size(void);
void*           pmem_base_va(void);   // direct map VA (== PA on xv6 identity map)

// Make [p, p+n) durable w.r.t. the PMEM persistence domain.
void            pmem_flush(void *p, uint64 n);
void            pmem_fence(void);
void            pmem_persist(void *p, uint64 n);  // flush + fence

// Development probe: write magic, persist, verify.
int             pmem_selftest(void);

#endif
