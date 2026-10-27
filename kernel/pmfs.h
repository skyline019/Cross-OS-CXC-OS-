// PMEM-native store — FCS dual-SB + page allocator (thesis/pmem-fs).
// Full POSIX FS comes later; this is the durable storage kernel.
#ifndef XV6_PMFS_H
#define XV6_PMFS_H

#include "types.h"
#include "pmdev.h"

enum {
  PMFS_MAGIC     = 0x32534650u,  // 'PFS2' — parallel to FCS 'FCS2'
  PMFS_FMT       = 2,            // v2: reserved inode/dentry meta pages
  PMFS_COMMIT_WD = 0xA55A5AA5u,  // same commit word as FCS
  PMFS_SB_HDR    = 48,           // on-disk header bytes (FCS SB_HDR)
  PMFS_SLOT_A    = 0,
  PMFS_SLOT_B    = 1,
  PMFS_DATA0     = 4,            // pages 2–3 = file meta; data from 4
};

// On-disk superblock header (48B). Alternate slots = page 0 / page 1.
struct pmfs_sb {
  uint32 magic;      // PMFS_MAGIC
  uint32 fmt;        // PMFS_FMT
  uint32 state;      // PMFS_COMMIT_WD when sealed
  uint32 chk;        // checksum with chk field zeroed
  uint32 tail;       // next free page id
  uint32 man_first;  // reserved (manifest chain; 0xFFFFFFFF = none)
  uint32 man_last;
  uint32 nman;
  uint32 n_inl;      // inline delta count (0 in M1)
  uint32 vers_lo;
  uint32 vers_hi;
  uint32 reserved;
};

struct pmfs {
  struct pmdev dev;
  int mounted;
  int emul;          // 1 = static EMUL buffer (no real NVDIMM yet)
  uint32 sb_slot;    // last durable slot (next write = sb_slot^1)
  uint64 epoch;      // logical version (FCS vers_)
  uint32 tail;
  uint32 npages;
};

void    pmfs_init(void);
int     pmfs_mounted(void);
struct pmfs* pmfs_store(void);

// Storage API (FCS Store::mount / write_sb / alloc subset).
int     pmfs_format(struct pmfs *fs);
int     pmfs_mount(struct pmfs *fs);
int     pmfs_commit(struct pmfs *fs);           // dual-slot SB flip
int     pmfs_alloc_pages(struct pmfs *fs, uint32 n, uint32 *out_pid);
int     pmfs_write_data_page(struct pmfs *fs, uint32 pid, const void *src);

// Self-test: format → write page → commit → remount → verify.
int     pmfs_storage_selftest(void);
int     pmfs_crash_selftest(void);

#endif
