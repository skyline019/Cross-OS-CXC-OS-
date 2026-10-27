// Persistent-memory window helpers (M0/M1 scaffold).
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"

static int pmem_ok = 0;

void
pmem_init(void)
{
  // M0: soft-disable until QEMU supplies RAM past PHYSTOP (see docs/QEMU-NVDIMM.md).
  // Mapping of [PMEMBASE, PMEMEND) is added in M1 inside kvmpagin/kvmmap.
  pmem_ok = 0;
  printk("pmem: stub init (base=0x%lx size=0x%lx) — awaiting QEMU window\n",
         (uint64)PMEMBASE, (uint64)PMEMSIZE);
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
  // RISC-V: prefer cbo.flush when available; M1 may use fence-only fallback.
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
  // Works for real PMEM and for EMUL DRAM stand-in (ordering + future flush).
  pmem_flush(p, n);
  pmem_fence();
}

int
pmem_selftest(void)
{
  if(!pmem_ok)
    return -1;
  return 0;
}
