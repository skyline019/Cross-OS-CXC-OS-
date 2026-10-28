// Page device over PMEM — borrow FCS BlockDev/RamDev semantics, not code.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmdev.h"

static void
pmdev_memcpy(void *dst, const void *src, uint32 n)
{
  memmove(dst, (void *)src, n);
}

int
pmdev_open(struct pmdev *d, void *base, uint64 nbytes)
{
  if (d == 0 || base == 0 || nbytes < PMDEV_PAGE ||
      nbytes / PMDEV_PAGE > PMDEV_MAX_PAGES)
    return -1;
  memset(d, 0, sizeof(*d));
  d->base = (uint8 *)base;
  d->nbytes = nbytes;
  d->npages = (uint32)(nbytes / PMDEV_PAGE);
  d->writes = 0;
  d->syncs = 0;
  return 0;
}

void
pmdev_close(struct pmdev *d)
{
  if (d)
    memset(d, 0, sizeof(*d));
}

static int
pmdev_valid(struct pmdev *d, pm_page_t pid, uint32 off, uint32 len)
{
  if (d == 0 || d->base == 0)
    return 0;
  if (pid >= d->npages)
    return 0;
  if ((uint64)off + len > PMDEV_PAGE)
    return 0;
  return 1;
}

int
pmdev_read(struct pmdev *d, pm_page_t pid, uint32 off, void *dst, uint32 len)
{
  if (!pmdev_valid(d, pid, off, len) || dst == 0)
    return -1;
  pmdev_memcpy(dst, d->base + (uint64)pid * PMDEV_PAGE + off, len);
  return 0;
}

int
pmdev_write(struct pmdev *d, pm_page_t pid, uint32 off, const void *src,
            uint32 len)
{
  if (!pmdev_valid(d, pid, off, len) || src == 0)
    return -1;
  pmdev_memcpy(d->base + (uint64)pid * PMDEV_PAGE + off, src, len);
  d->writes++;
  if (len)
    d->dirty[pid >> 6] |= 1ULL << (pid & 63);
  return 0;
}

int
pmdev_write_page(struct pmdev *d, pm_page_t pid, const void *src)
{
  if (pmdev_write(d, pid, 0, src, PMDEV_PAGE) != 0)
    return -1;
  pmem_persist(d->base + (uint64)pid * PMDEV_PAGE, PMDEV_PAGE);
  d->dirty[pid >> 6] &= ~(1ULL << (pid & 63));
  return 0;
}

void
pmdev_sync(struct pmdev *d)
{
  if (d == 0 || d->base == 0)
    return;
  uint32 pid;
  // Only dirty pages; flushing the whole window on every sync makes the
  // apparent cost depend on configured device capacity rather than writes.
  for (pid = 0; pid < d->npages; pid++) {
    uint64 mask = 1ULL << (pid & 63);
    if (d->dirty[pid >> 6] & mask) {
      pmem_persist(d->base + (uint64)pid * PMDEV_PAGE, PMDEV_PAGE);
      d->dirty[pid >> 6] &= ~mask;
    }
  }
  pmem_fence();
  d->syncs++;
}

void *
pmdev_raw(struct pmdev *d, pm_page_t pid)
{
  if (d == 0 || d->base == 0 || pid >= d->npages)
    return 0;
  return d->base + (uint64)pid * PMDEV_PAGE;
}

uint32
pmdev_capacity(struct pmdev *d)
{
  return d ? d->npages : 0;
}
