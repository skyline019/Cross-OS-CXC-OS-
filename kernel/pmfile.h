// PMFS file subsystem v2: dirs + extents over KV/CDC/stream.
#ifndef XV6_PMFILE_H
#define XV6_PMFILE_H

#include "types.h"

enum {
  PMFILE_MAX_INO   = 40,   // fits one meta page with smaller extent slots
  PMFILE_MAX_FD    = 8,
  PMFILE_NAME      = 28,
  PMFILE_MAX_EXT   = 4,    // chunks per file (page-budget vs inode count)
  PMFILE_MAX_SIZE  = 1024, // syscall write/read cap
  PMFILE_INODE_PG  = 2,
  PMFILE_DENT_PG   = 3,
  PMFILE_TYPE_REG  = 1,
  PMFILE_TYPE_DIR  = 2,
  PMFILE_ROOT_INO  = 1,
};

struct pmfile_extent {
  uint64 key;    // KV chunk key
  uint32 len;
  uint32 off;    // file offset
};

struct pmfile_inode {
  uint32 used;
  uint32 type;   // REG / DIR
  uint32 size;
  uint32 mtime;
  uint32 nlink;
  uint32 next_seq;
  uint32 nextent;
  uint32 pad;
  struct pmfile_extent ext[PMFILE_MAX_EXT];
};

struct pmfile_dent {
  uint32 ino;
  uint32 parent; // 0 = unused; root parent = 0
  char   name[PMFILE_NAME];
};

void pmfile_init(void);
int  pmfile_selftest(void);
int  pmfile_chaos_selftest(void);
int  pmfile_edge_selftest(void);

int  pmfile_create(const char *path);
int  pmfile_mkdir(const char *path);
int  pmfile_open(const char *path);
int  pmfile_close(int fd);
int  pmfile_read(int fd, void *dst, int cap, uint32 *out_len);
int  pmfile_write(int fd, const void *src, int len); // CDC+stream fused
int  pmfile_fsync(int fd);
int  pmfile_unlink(const char *path);
int  pmfile_readdir(const char *path, char *dst, int cap); // newline-sep names
int  pmfile_stat(const char *path, uint32 *size, uint32 *type);
void pmfile_set_ablation(int cdc_on, int batch_seal);
int  pmfile_ablation_selftest(void);
int  pmfile_remount_selftest(void); // sync后 remount 字节等价
int  pmfile_fault_selftest(void);   // sealmid + halfpage chk

#endif
