// Minimal inode/dentry file layer on PMEM (no bio / no virtio).
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmfile.h"
#include "pmcontract.h"

static struct pmfile_inode inodes[PMFILE_MAX_INO];
static struct pmfile_dent  dents[PMFILE_MAX_INO];
static struct {
  int ino;   // 1..N, 0 = free slot
  int dirty;
} ofile[PMFILE_MAX_FD];

static int
name_eq(const char *a, const char *b)
{
  int i;
  for(i = 0; i < PMFILE_NAME; i++){
    if(a[i] != b[i])
      return 0;
    if(a[i] == 0)
      return 1;
  }
  return 1;
}

static void
name_copy(char *dst, const char *src)
{
  int i;
  for(i = 0; i < PMFILE_NAME - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
  for(; i < PMFILE_NAME; i++)
    dst[i] = 0;
}

static int
find_dent(const char *path)
{
  int i;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(dents[i].ino != 0 && name_eq(dents[i].name, path))
      return i;
  }
  return -1;
}

static int
pmfile_load(void)
{
  struct pmfs *fs = pmfs_store();
  if(fs == 0 || !fs->mounted)
    return -1;
  if(pmdev_read(&fs->dev, PMFILE_INODE_PG, 0, inodes, sizeof(inodes)) != 0)
    return -1;
  if(pmdev_read(&fs->dev, PMFILE_DENT_PG, 0, dents, sizeof(dents)) != 0)
    return -1;
  return 0;
}

static int
pmfile_store_meta(void)
{
  struct pmfs *fs = pmfs_store();
  static uint8 page[PMDEV_PAGE];

  if(fs == 0 || !fs->mounted)
    return -1;

  memset(page, 0, sizeof(page));
  memmove(page, inodes, sizeof(inodes));
  // Ordered persist: data (inode page) then fence via write_page
  if(pmdev_write_page(&fs->dev, PMFILE_INODE_PG, page) != 0)
    return -1;

  memset(page, 0, sizeof(page));
  memmove(page, dents, sizeof(dents));
  if(pmdev_write_page(&fs->dev, PMFILE_DENT_PG, page) != 0)
    return -1;

  // SB commit after meta durable
  if(pmfs_commit(fs) != 0)
    return -1;
  return 0;
}

void
pmfile_init(void)
{
  int i;
  struct pmfs *fs = pmfs_store();

  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  for(i = 0; i < PMFILE_MAX_FD; i++){
    ofile[i].ino = 0;
    ofile[i].dirty = 0;
  }

  if(fs == 0 || !fs->mounted)
    return;

  // Ensure meta pages are inside capacity; format path zeros device.
  if(fs->npages <= PMFILE_DENT_PG)
    return;

  if(pmfile_load() != 0){
    memset(inodes, 0, sizeof(inodes));
    memset(dents, 0, sizeof(dents));
    pmfile_store_meta();
  }
}

int
pmfile_create(const char *path)
{
  int di, ii, i;

  if(path == 0 || path[0] == 0)
    return -1;
  if(find_dent(path) >= 0)
    return -1; // exists

  di = -1;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(dents[i].ino == 0){ di = i; break; }
  }
  ii = -1;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(!inodes[i].used){ ii = i; break; }
  }
  if(di < 0 || ii < 0)
    return -1;

  memset(&inodes[ii], 0, sizeof(inodes[ii]));
  inodes[ii].used = 1;
  inodes[ii].size = 0;
  inodes[ii].mtime = 1;
  inodes[ii].gen = 1;

  dents[di].ino = (uint32)(ii + 1);
  name_copy(dents[di].name, path);
  return 0;
}

int
pmfile_open(const char *path)
{
  int di, fd, ino;

  di = find_dent(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if(ino < 1 || ino > PMFILE_MAX_INO || !inodes[ino - 1].used)
    return -1;

  for(fd = 0; fd < PMFILE_MAX_FD; fd++){
    if(ofile[fd].ino == 0){
      ofile[fd].ino = ino;
      ofile[fd].dirty = 0;
      return fd;
    }
  }
  return -1;
}

int
pmfile_close(int fd)
{
  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0)
    return -1;
  ofile[fd].ino = 0;
  ofile[fd].dirty = 0;
  return 0;
}

int
pmfile_read(int fd, void *dst, int cap, uint32 *out_len)
{
  int ino;
  struct pmfile_inode *ip;
  uint32 n;

  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0 || dst == 0 || cap < 0)
    return -1;
  ino = ofile[fd].ino;
  ip = &inodes[ino - 1];
  if(!ip->used)
    return -1;
  n = ip->size;
  if((uint32)cap < n)
    n = (uint32)cap;
  memmove(dst, ip->data, n);
  if(out_len)
    *out_len = n;
  return 1;
}

int
pmfile_write(int fd, const void *src, int len)
{
  int ino;
  struct pmfile_inode *ip;

  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0 || src == 0)
    return -1;
  if(len < 0 || len > PMFILE_MAX_SIZE)
    return -1;
  ino = ofile[fd].ino;
  ip = &inodes[ino - 1];
  if(!ip->used)
    return -1;
  memmove(ip->data, (void *)src, (uint32)len);
  ip->size = (uint32)len;
  ip->mtime++;
  ofile[fd].dirty = 1;
  return len;
}

int
pmfile_fsync(int fd)
{
  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0)
    return -1;
  if(pmfile_store_meta() != 0)
    return -1;
  ofile[fd].dirty = 0;
  return 0;
}

int
pmfile_unlink(const char *path)
{
  int di, ino, fd;

  di = find_dent(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  for(fd = 0; fd < PMFILE_MAX_FD; fd++){
    if(ofile[fd].ino == ino){
      ofile[fd].ino = 0;
      ofile[fd].dirty = 0;
    }
  }
  if(ino >= 1 && ino <= PMFILE_MAX_INO)
    memset(&inodes[ino - 1], 0, sizeof(inodes[0]));
  dents[di].ino = 0;
  memset(dents[di].name, 0, PMFILE_NAME);
  return pmfile_store_meta();
}

int
pmfile_selftest(void)
{
  int fd;
  char buf[64];
  uint32 len = 0;
  int hit;

  if(pmfile_create("/pmfs/hello.txt") != 0){
    // may exist after remount — unlink first
    pmfile_unlink("/pmfs/hello.txt");
    if(pmfile_create("/pmfs/hello.txt") != 0){
      pmcontract_fail("FILE", 1);
      return -1;
    }
  }
  fd = pmfile_open("/pmfs/hello.txt");
  if(fd < 0){ pmcontract_fail("FILE", 2); return -2; }
  if(pmfile_write(fd, "hello-pmfs", 10) != 10){
    pmcontract_fail("FILE", 3); return -3;
  }
  if(pmfile_fsync(fd) != 0){ pmcontract_fail("FILE", 4); return -4; }
  pmfile_close(fd);

  // Simulate remount: reload meta from durable pages
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  if(pmfile_load() != 0){ pmcontract_fail("FILE", 5); return -5; }

  fd = pmfile_open("/pmfs/hello.txt");
  if(fd < 0){ pmcontract_fail("FILE", 6); return -6; }
  hit = pmfile_read(fd, buf, sizeof(buf), &len);
  if(hit != 1 || len != 10 || buf[0] != 'h'){
    pmcontract_fail("FILE", 7); return -7;
  }
  pmfile_close(fd);

  // Uncommitted write must not survive reload
  fd = pmfile_open("/pmfs/hello.txt");
  if(fd < 0){ pmcontract_fail("FILE", 8); return -8; }
  if(pmfile_write(fd, "DIRTY", 5) != 5){ pmcontract_fail("FILE", 9); return -9; }
  // no fsync
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  if(pmfile_load() != 0){ pmcontract_fail("FILE", 10); return -10; }
  fd = pmfile_open("/pmfs/hello.txt");
  if(fd < 0){ pmcontract_fail("FILE", 11); return -11; }
  hit = pmfile_read(fd, buf, sizeof(buf), &len);
  if(hit != 1 || len != 10 || buf[0] != 'h'){
    pmcontract_fail("FILE", 12); return -12;
  }
  pmfile_close(fd);
  pmfile_unlink("/pmfs/hello.txt");

  printk("ONBOX_PMFS_FILE_PASS ops=create_write_fsync_reload\n");
  pmcontract_mark("FILE", 1);
  return 0;
}
