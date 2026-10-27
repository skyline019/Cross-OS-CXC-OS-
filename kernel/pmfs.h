// PMEM-native filesystem (thesis/pmem-fs). Scaffold only in M0.
#ifndef XV6_PMFS_H
#define XV6_PMFS_H

#include "types.h"

#define PMFS_MAGIC 0x504D4653u  /* 'PMFS' */

struct pmfs_super {
  uint magic;
  uint epoch;
  uint root_inum;
  uint checksum;
  // Extended fields filled in M2.
  uint inode_off;
  uint inode_bytes;
  uint data_off;
  uint data_bytes;
};

void    pmfs_init(void);     // called after pmem_init
int     pmfs_mounted(void);

#endif
