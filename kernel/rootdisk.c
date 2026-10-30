// A deliberately small root-disk switch. It does not put PMEM behind bio:
// PMFS continues to use its own byte-addressable persistence path.
#include "types.h"
#include "riscv.h"
#include "defs.h"

void
rootdisk_init(void)
{
#ifdef XV6_DISK_NVME
  nvme_disk_init();
#else
  virtio_disk_init();
#endif
}

void
rootdisk_rw(struct buf *b, int write)
{
#ifdef XV6_DISK_NVME
  nvme_disk_rw(b, write);
#else
  virtio_disk_rw(b, write);
#endif
}

void
rootdisk_log_barrier(void)
{
#ifdef XV6_DISK_NVME
  nvme_disk_flush();
#else
  // Preserve the historical virtio behavior. Its driver currently does not
  // negotiate FLUSH, so this path is NOT an equivalent persistence contract.
#endif
}
