// Persistent-memory window helpers.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmcontract.h"

// Word 0: MAP probe. Word 1: PMEM1 selftest. Word 2: durable across power-loss (PMEM2).
#define PMEM_MAGIC      0xA5A5FCFC55443322ULL
#define PMEM1_MAGIC     0xBEEFCAFE01020304ULL
#define PMEM2_DURABLE   0x504D454D32000001ULL  // "PMEM2\0\0\1"

static int pmem_ok = 0;

void
pmem_init(void)
{
  pmem_ok = 0;

#ifdef PMEM_MAP
  {
    volatile uint64 *p = (volatile uint64 *)PMEMBASE;
    uint64 got;
    int survived = 0;

    // PMEM2 check BEFORE any wipe: file-backed guest RAM keeps word 2.
    if(p[2] == PMEM2_DURABLE)
      survived = 1;

    p[0] = 0;
    pmem_persist((void *)PMEMBASE, 8);
    p[0] = PMEM_MAGIC;
    pmem_persist((void *)PMEMBASE, 8);
    got = p[0];
    if(got == PMEM_MAGIC){
      pmem_ok = 1;
      printk("pmem: MAP ready base=0x%lx size=0x%lx\n",
             (uint64)PMEMBASE, (uint64)PMEMSIZE);
      if(survived){
        printk("ONBOX_PMFS_PMEM2_PASS persist=ok\n");
        // not a gate-required stage; mark optional
      }
    } else {
      printk("pmem: MAP probe mismatch — falling back to EMUL\n");
    }
  }
#else
  printk("pmem: stub (no PMEM_MAP) base=0x%lx — EMUL path\n",
         (uint64)PMEMBASE);
#endif
}

int
pmem_ready(void)
{
  return pmem_ok;
}

uint64
pmem_base_pa(void)
{
  return PMEMBASE;
}

uint64
pmem_size(void)
{
  return PMEMSIZE;
}

void*
pmem_base_va(void)
{
  return (void*)PMEMBASE;
}

void
pmem_flush(void *p, uint64 n)
{
  (void)p;
  (void)n;
  __sync_synchronize();
}

void
pmem_fence(void)
{
  __sync_synchronize();
  asm volatile("fence rw, rw" ::: "memory");
}

void
pmem_persist(void *p, uint64 n)
{
  pmem_flush(p, n);
  pmem_fence();
}

int
pmem_selftest(void)
{
  volatile uint64 *p;
  uint64 got;

  if(!pmem_ok)
    return -1;

  p = (volatile uint64 *)PMEMBASE;
  p[1] = PMEM1_MAGIC;
  pmem_persist((void *)(PMEMBASE + 8), 8);
  got = p[1];
  if(got != PMEM1_MAGIC)
    return -2;
  return 0;
}

void
pmem_contract_map(void)
{
  volatile uint64 *p;
  int rc;
  if(!pmem_ok)
    return;
  rc = pmem_selftest();
  if(rc != 0){
    pmcontract_fail("PMEM1", rc < 0 ? -rc : rc);
    return;
  }
  pmcontract_pass("PMEM1", "magic=ok");

  // Plant durable marker for next cold boot (PMEM2 / memory-backend-file).
  p = (volatile uint64 *)PMEMBASE;
  p[2] = PMEM2_DURABLE;
  pmem_persist((void *)(PMEMBASE + 16), 8);
}
