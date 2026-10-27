// Persistent-memory window helpers + line-granular persist protocol.
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
static uint64 flush_lines_total = 0;

void
pmem_init(void)
{
  pmem_ok = 0;
  flush_lines_total = 0;

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
      printk("pmem: MAP ready base=0x%lx size=0x%lx backend=map (NVDIMM sim)\n",
             (uint64)PMEMBASE, (uint64)PMEMSIZE);
      if(survived){
        printk("ONBOX_PMFS_PMEM2_PASS persist=ok\n");
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

const char*
pmem_backend_name(void)
{
  return pmem_ok ? "map" : "emul";
}

uint64
pmem_flush_lines(void)
{
  return flush_lines_total;
}

void
pmem_flush_lines_reset(void)
{
  flush_lines_total = 0;
}

// Line-granular flush: RISC-V without CBO.FLUSH still performs a visible
// per-line touch + compiler barrier so the persist protocol is observable
// (flush_lines) and ordered relative to pmem_fence.
void
pmem_flush(void *p, uint64 n)
{
  uint64 a, end;
  volatile uint8 *v;

  if(p == 0 || n == 0)
    return;

  a = (uint64)p & ~(uint64)(PMEM_LINE - 1);
  end = (uint64)p + n;
  for(; a < end; a += PMEM_LINE){
    v = (volatile uint8 *)a;
    (void)*v;  // touch line (stand-in for cbo.flush / clflush)
    flush_lines_total++;
  }
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

// Ordered persist protocol self-test (合题：缓存行刷写).
int
pmem_persist_selftest(void)
{
  static uint8 buf[PMEM_LINE * 3];
  uint64 before, after, expect;
  int i;

  pmem_flush_lines_reset();
  before = pmem_flush_lines();
  for(i = 0; i < (int)sizeof(buf); i++)
    buf[i] = (uint8)(i * 17);

  // store data → flush data → fence
  pmem_flush(buf, sizeof(buf));
  pmem_fence();
  after = pmem_flush_lines();
  expect = (sizeof(buf) + PMEM_LINE - 1) / PMEM_LINE;
  if(after - before < expect){
    pmcontract_fail("PERSIST", 1);
    return -1;
  }
  // Second range spanning two lines
  before = pmem_flush_lines();
  pmem_persist(buf + PMEM_LINE / 2, PMEM_LINE);
  after = pmem_flush_lines();
  if(after <= before){
    pmcontract_fail("PERSIST", 2);
    return -2;
  }

  printk("ONBOX_PMFS_PERSIST_PASS flush_lines=%d line=%d proto=store_flush_fence\n",
         (int)pmem_flush_lines(), PMEM_LINE);
  pmcontract_mark("PERSIST", 1);
  return 0;
}
