// PMEM filesystem scaffold (M0). Dual-superblock logic lands in M2/M3.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmfs.h"

static int mounted = 0;

void
pmfs_init(void)
{
  mounted = 0;
  if(!pmem_ready()){
    printk("pmfs: skip mount (pmem not ready)\n");
    return;
  }
  // M2: locate dual SB, verify, select epoch, rebuild caches.
  printk("pmfs: scaffold ready\n");
}

int
pmfs_mounted(void)
{
  return mounted;
}
