// Syscalls for PMEM file object layer.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "pmfile.h"

uint64
sys_pmfs_create(void)
{
  char path[PMFILE_NAME];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  return pmfile_create(path);
}

uint64
sys_pmfs_fopen(void)
{
  char path[PMFILE_NAME];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  return pmfile_open(path);
}

uint64
sys_pmfs_fread(void)
{
  int fd, cap;
  uint64 udst, ulen;
  static char buf[PMFILE_MAX_SIZE];
  uint32 len = 0;
  int hit;
  struct proc *p = myproc();

  argint(0, &fd);
  argaddr(1, &udst);
  argint(2, &cap);
  argaddr(3, &ulen);
  if(cap < 0 || cap > PMFILE_MAX_SIZE)
    return -1;
  hit = pmfile_read(fd, buf, cap, &len);
  if(hit < 0)
    return -1;
  if(hit == 1 && len > 0){
    if(copyout(p->pagetable, p->sz, udst, buf, len) < 0)
      return -1;
  }
  if(ulen){
    if(copyout(p->pagetable, p->sz, ulen, (char *)&len, sizeof(len)) < 0)
      return -1;
  }
  return hit;
}

uint64
sys_pmfs_fwrite(void)
{
  int fd, len;
  uint64 usrc;
  static char buf[PMFILE_MAX_SIZE];

  argint(0, &fd);
  argaddr(1, &usrc);
  argint(2, &len);
  if(len < 0 || len > PMFILE_MAX_SIZE)
    return -1;
  if(len > 0){
    if(copyin(myproc()->pagetable, myproc()->sz, buf, usrc, (uint64)len) < 0)
      return -1;
  }
  return pmfile_write(fd, buf, len);
}

uint64
sys_pmfs_fsync(void)
{
  int fd;
  argint(0, &fd);
  return pmfile_fsync(fd);
}

uint64
sys_pmfs_funlink(void)
{
  char path[PMFILE_NAME];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  return pmfile_unlink(path);
}

uint64
sys_pmfs_fclose(void)
{
  int fd;
  argint(0, &fd);
  return pmfile_close(fd);
}

uint64
sys_pmfs_mkdir(void)
{
  char path[PMFILE_NAME];
  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  return pmfile_mkdir(path);
}

uint64
sys_pmfs_readdir(void)
{
  char path[PMFILE_NAME];
  uint64 udst;
  int cap;
  char buf[256];
  struct proc *p = myproc();

  if(argstr(0, path, sizeof(path)) < 0)
    return -1;
  argaddr(1, &udst);
  argint(2, &cap);
  if(cap < 0 || cap > (int)sizeof(buf))
    return -1;
  if(pmfile_readdir(path, buf, cap > 0 ? cap : (int)sizeof(buf)) != 0)
    return -1;
  if(copyout(p->pagetable, p->sz, udst, buf, strlen(buf) + 1) < 0)
    return -1;
  return 0;
}
