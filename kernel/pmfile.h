// Minimal PMEM file object layer (parallel to virtio root FS).
#ifndef XV6_PMFILE_H
#define XV6_PMFILE_H

#include "types.h"

enum {
  PMFILE_MAX_INO  = 32,  // one page of 64B inodes
  PMFILE_MAX_FD   = 8,
  PMFILE_NAME     = 28,
  PMFILE_MAX_SIZE = 48,
  PMFILE_INODE_PG = 2,  // reserved meta pages (before data)
  PMFILE_DENT_PG  = 3,
};

struct pmfile_inode {
  uint32 used;
  uint32 size;
  uint32 mtime;
  uint32 gen;
  uint8  data[PMFILE_MAX_SIZE];
};

struct pmfile_dent {
  uint32 ino;          // 0 = empty; else 1..PMFILE_MAX_INO
  char   name[PMFILE_NAME];
};

void pmfile_init(void);
int  pmfile_selftest(void);

int  pmfile_create(const char *path);
int  pmfile_open(const char *path);          // returns fd >= 0
int  pmfile_close(int fd);
int  pmfile_read(int fd, void *dst, int cap, uint32 *out_len);
int  pmfile_write(int fd, const void *src, int len);
int  pmfile_fsync(int fd);
int  pmfile_unlink(const char *path);

#endif
