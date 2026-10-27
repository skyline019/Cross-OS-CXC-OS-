// PMFS user syscalls: parallel KV store (does not replace virtio root fs).
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "pmalgo.h"
#include "pmfs.h"

uint64
sys_pmfs_put(void)
{
  uint64 key, uval;
  int len;
  char buf[64];
  struct pm_algo *a = pm_algo_global();

  argaddr(0, &key);
  argaddr(1, &uval);
  argint(2, &len);
  if(len < 0 || len > 64)
    return -1;
  if(a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  if(len > 0){
    if(copyin(myproc()->pagetable, myproc()->sz, buf, uval, (uint64)len) < 0)
      return -1;
  }
  return pm_put(a, key, buf, (uint32)len);
}

uint64
sys_pmfs_get(void)
{
  uint64 key, udst, ulen;
  int cap;
  char buf[64];
  uint32 len = 0;
  int hit;
  struct pm_algo *a = pm_algo_global();
  struct proc *p = myproc();

  argaddr(0, &key);
  argaddr(1, &udst);
  argint(2, &cap);
  argaddr(3, &ulen);
  if(cap < 0 || cap > 64)
    return -1;
  if(a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  hit = pm_get(a, key, buf, (uint32)cap, &len);
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
sys_pmfs_seal(void)
{
  struct pm_algo *a = pm_algo_global();
  if(a == 0 || a->fs == 0 || !a->fs->mounted)
    return -1;
  return pm_seal(a);
}

uint64
sys_pmfs_stat(void)
{
  uint64 uwa;
  struct pm_wa w;
  struct pm_algo *a = pm_algo_global();
  struct proc *p = myproc();

  argaddr(0, &uwa);
  if(a == 0)
    return -1;
  w = pm_wa_snapshot(a);
  if(copyout(p->pagetable, p->sz, uwa, (char *)&w, sizeof(w)) < 0)
    return -1;
  return 0;
}
