// Dedicated prototype file API; it is not the xv6 POSIX file-descriptor API.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "pmfile.h"
#include "pmfs.h"

uint64
sys_pmfs_create(void)
{
  char path[PMFILE_NAME];
  int rc;
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  pmfs_lock();
  rc = pmfile_create(path);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_fopen(void)
{
  char path[PMFILE_NAME];
  int rc;
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  pmfs_lock();
  rc = pmfile_open(path);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_fclose(void)
{
  int fd, rc;
  argint(0, &fd);
  pmfs_lock();
  rc = pmfile_close(fd);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_fwrite(void)
{
  int fd, len, rc;
  uint64 src;
  char buf[PMFILE_MAX_SIZE];
  struct proc *p = myproc();
  argint(0, &fd);
  argaddr(1, &src);
  argint(2, &len);
  if (len < 0 || len > (int)sizeof(buf))
    return -1;
  if (len > 0 && copyin(p->pagetable, p->sz, buf, src, len) < 0)
    return -1;
  pmfs_lock();
  rc = pmfile_write(fd, buf, len);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_fread(void)
{
  int fd, cap, rc;
  uint64 dst, ulen;
  uint32 len = 0;
  char buf[PMFILE_MAX_SIZE];
  struct proc *p = myproc();
  argint(0, &fd);
  argaddr(1, &dst);
  argint(2, &cap);
  argaddr(3, &ulen);
  if (cap < 0 || cap > (int)sizeof(buf))
    return -1;
  pmfs_lock();
  rc = pmfile_read(fd, buf, cap, &len);
  pmfs_unlock();
  if (rc < 0)
    return -1;
  if (len > 0 && copyout(p->pagetable, p->sz, dst, buf, len) < 0)
    return -1;
  if (ulen && copyout(p->pagetable, p->sz, ulen, (char *)&len, sizeof(len)) < 0)
    return -1;
  return rc;
}

uint64
sys_pmfs_fsync(void)
{
  int fd, rc;
  argint(0, &fd);
  pmfs_lock();
  rc = pmfile_fsync(fd);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_funlink(void)
{
  char path[PMFILE_NAME];
  int rc;
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  pmfs_lock();
  rc = pmfile_unlink(path);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_mkdir(void)
{
  char path[PMFILE_NAME];
  int rc;
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  pmfs_lock();
  rc = pmfile_mkdir(path);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_readdir(void)
{
  char path[PMFILE_NAME], buf[256];
  uint64 dst;
  int cap, rc;
  struct proc *p = myproc();
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  argaddr(1, &dst);
  argint(2, &cap);
  if (cap < 2 || cap > (int)sizeof(buf))
    return -1;
  pmfs_lock();
  rc = pmfile_readdir(path, buf, cap);
  pmfs_unlock();
  if (rc < 0 || copyout(p->pagetable, p->sz, dst, buf, strlen(buf) + 1) < 0)
    return -1;
  return 0;
}

uint64
sys_pmfs_fstat(void)
{
  char path[PMFILE_NAME];
  uint64 usize, utype;
  uint32 size = 0, type = 0;
  int rc;
  struct proc *p = myproc();
  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  argaddr(1, &usize);
  argaddr(2, &utype);
  pmfs_lock();
  rc = pmfile_stat(path, &size, &type);
  pmfs_unlock();
  if (rc < 0 ||
      (usize &&
       copyout(p->pagetable, p->sz, usize, (char *)&size, sizeof(size)) < 0) ||
      (utype &&
       copyout(p->pagetable, p->sz, utype, (char *)&type, sizeof(type)) < 0))
    return -1;
  return 0;
}
