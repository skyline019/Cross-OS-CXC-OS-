// FCS-inspired page device over PMEM (thesis/pmem-fs).
// Mirrors e:\FCS embdb BlockDev+RamDev: page-granular R/W, sync = persist fence.
#ifndef XV6_PMDEV_H
#define XV6_PMDEV_H

#include "types.h"

enum {
  PMDEV_PAGE = 4096,
  PMDEV_MAX_PAGES = 8192, // 32 MiB: size of the current mapped window
  PMDEV_DIRTY_WORDS = (PMDEV_MAX_PAGES + 63) / 64,
};

typedef uint32 pm_page_t;

struct pmdev {
  uint8 *base; // VA of byte-addressable store
  uint64 nbytes;
  uint32 npages;
  uint32 writes;
  uint32 syncs;
  uint64 dirty[PMDEV_DIRTY_WORDS];
};

// Bind to a contiguous region (PMEM window or EMUL buffer).
int pmdev_open(struct pmdev *d, void *base, uint64 nbytes);
void pmdev_close(struct pmdev *d);

int pmdev_read(struct pmdev *d, pm_page_t pid, uint32 off, void *dst,
               uint32 len);
int pmdev_write(struct pmdev *d, pm_page_t pid, uint32 off, const void *src,
                uint32 len);
// Whole-page write then persist that page (FCS-like durable store).
int pmdev_write_page(struct pmdev *d, pm_page_t pid, const void *src);
// Persist only pages dirtied by pmdev_write, then fence.
void pmdev_sync(struct pmdev *d);

void *pmdev_raw(struct pmdev *d, pm_page_t pid); // direct pointer; NULL if OOB
uint32 pmdev_capacity(struct pmdev *d);

#endif
