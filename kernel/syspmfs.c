// Process-facing PMFS KV calls. The global lock protects the shared memtable,
// index and on-device transaction state across harts and processes.
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
  int len, rc;
  char buf[PM_VAL_MAX];
  struct pm_algo *a = pm_algo_global();

  argaddr(0, &key);
  argaddr(1, &uval);
  argint(2, &len);
  if (len < 0 || len > PM_VAL_MAX || pm_algo_is_internal_key(key))
    return -1;
  if (len > 0 && copyin(myproc()->pagetable, myproc()->sz, buf, uval, len) < 0)
    return -1;
  pmfs_lock();
  rc = (!pmfs_mounted()) ? -1 : pm_put(a, key, buf, (uint32)len);
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_get(void)
{
  uint64 key, udst, ulen;
  int cap, hit;
  char buf[PM_VAL_MAX];
  uint32 len = 0;
  struct proc *p = myproc();

  argaddr(0, &key);
  argaddr(1, &udst);
  argint(2, &cap);
  argaddr(3, &ulen);
  if (cap < 0 || cap > PM_VAL_MAX || pm_algo_is_internal_key(key))
    return -1;
  pmfs_lock();
  hit = !pmfs_mounted() ? -1
                        : pm_get(pm_algo_global(), key, buf, (uint32)cap, &len);
  pmfs_unlock();
  if (hit < 0)
    return -1;
  if (hit == 1 && len > 0 && copyout(p->pagetable, p->sz, udst, buf, len) < 0)
    return -1;
  if (ulen && copyout(p->pagetable, p->sz, ulen, (char *)&len, sizeof(len)) < 0)
    return -1;
  return hit;
}

uint64
sys_pmfs_seal(void)
{
  int rc;
  pmfs_lock();
  rc = !pmfs_mounted() ? -1 : pm_seal(pm_algo_global());
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_stat(void)
{
  uint64 uwa;
  struct pm_wa w;
  struct proc *p = myproc();
  argaddr(0, &uwa);
  pmfs_lock();
  if (!pmfs_mounted()) {
    pmfs_unlock();
    return -1;
  }
  w = pm_wa_snapshot(pm_algo_global());
  pmfs_unlock();
  return copyout(p->pagetable, p->sz, uwa, (char *)&w, sizeof(w)) < 0 ? -1 : 0;
}

uint64
sys_pmfs_format(void)
{
  int rc;
  pmfs_lock();
  rc = pmfs_format_unmounted();
  pmfs_unlock();
  return rc;
}

uint64
sys_pmfs_fault(void)
{
  int stage, skip, rc;
  argint(0, &stage);
  argint(1, &skip);
  pmfs_lock();
  rc = pmfs_fault_arm(stage, skip);
  pmfs_unlock();
  return rc;
}
