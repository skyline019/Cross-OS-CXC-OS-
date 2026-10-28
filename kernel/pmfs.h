// Versioned PMFS checkpoint and append-only page allocator.
#ifndef XV6_PMFS_H
#define XV6_PMFS_H

#include "types.h"
#include "pmdev.h"

enum {
  PMFS_MAGIC = 0x32534650u,
  PMFS_FMT = 3, // incompatible with the destructive v2 prototype
  PMFS_COMMIT_WD = 0xA55A5AA5u,
  PMFS_SB_HDR = 48,
  PMFS_SLOT_A = 0,
  PMFS_SLOT_B = 1,
  PMFS_DATA0 = 4, // 0-1 checkpoints; 2-3 reserved
};
#define PMFS_NIL 0xffffffffu

// state+chk occupy one aligned 64-bit commit word at byte offset 8.
// Data pages and new metadata roots must be persisted before publishing it.
struct pmfs_sb {
  uint32 magic;
  uint32 fmt;
  uint32 state;
  uint32 chk;
  uint32 tail;
  uint32 inode_page;
  uint32 dent_page;
  uint32 run_head;
  uint32 reserved0;
  uint32 vers_lo;
  uint32 vers_hi;
  uint32 reserved1;
};

struct pmfs {
  struct pmdev dev;
  int mounted;
  int emul;
  uint32 sb_slot;
  uint64 epoch;
  uint32 tail;
  uint32 npages;
  uint32 inode_page;
  uint32 dent_page;
  uint32 run_head;
};

void pmfs_init(void);
int pmfs_mounted(void);
struct pmfs *pmfs_store(void);
int pmfs_format_unmounted(void);
void pmfs_lock(void);
void pmfs_unlock(void);
int pmfs_fault_arm(int stage, int skip);

int pmfs_format(struct pmfs *fs);
int pmfs_mount(struct pmfs *fs);
int pmfs_commit(struct pmfs *fs);
int pmfs_alloc_pages(struct pmfs *fs, uint32 n, uint32 *out_pid);
int pmfs_write_data_page(struct pmfs *fs, uint32 pid, const void *src);

int pmfs_storage_selftest(void);
int pmfs_crash_selftest(void);

#endif
