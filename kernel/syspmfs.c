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
#include "pmem.h"
#include "pmstats.h"
#include "pmtrace.h"

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
sys_pmfs_compact(void)
{
  int rc;
  pmfs_lock();
  rc = !pmfs_mounted() ? PM_COMPACT_ERROR : pm_compact(pm_algo_global());
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

// Snapshot software work counters under the PMFS lock. These are volatile
// requests/copies, not actual NAND erase counts or persistent-media traffic.
uint64
sys_pmfs_metrics(void)
{
  uint64 dst;
  struct pmfs_metrics m;
  struct pm_algo *a = pm_algo_global();
  struct pmfs *fs = pmfs_store();
  int capacity;
  argaddr(0, &dst);
  argint(1, &capacity);
  // Preserve the original v1 prefix for older callers; new counters are an
  // optional trailing extension rather than a breaking struct-size change.
  if (capacity < (int)__builtin_offsetof(struct pmfs_metrics, gc_scans))
    return -1;
  pmfs_lock();
  if (!pmfs_mounted()) {
    pmfs_unlock();
    return -1;
  }
  memset(&m, 0, sizeof(m));
  m.version = 1;
  m.dev_read_bytes = fs->dev.read_bytes;
  m.dev_write_bytes = fs->dev.write_bytes;
  m.dev_page_writes = fs->dev.page_writes;
  m.checkpoint_word_stores = fs->dev.checkpoint_word_stores;
  m.requested_flush_lines = pmem_flush_lines();
  m.file_write_bytes = a->file_write_bytes;
  m.file_identical_bytes = a->file_identical_bytes;
  m.file_identical_ops = a->file_identical_ops;
  m.file_cdc_attempts = a->file_cdc_attempts;
  m.file_cdc_planned_chunks = a->file_cdc_planned_chunks;
  m.file_fixed_attempts = a->file_fixed_attempts;
  m.kv_exact_hits = a->exact_hits;
  m.kv_fallback_run_probes = a->fallback_run_probes;
  m.kv_posting_skips = a->posting_skips;
  m.kv_page_reads = a->wa.page_reads;
  m.gc_scans = fs->gc_scans;
  m.gc_trace_runs = fs->gc_trace_runs;
  m.gc_reused_pages = fs->gc_reused_pages;
  m.gc_failed_scans = fs->gc_failed_scans;
  m.fs_tail_pages = fs->tail;
  m.fs_capacity_pages = fs->npages;
  m.kv_runs = a->nruns;
  m.compact_calls = a->compact_calls;
  m.compact_input_records = a->compact_input_records;
  m.compact_output_records = a->compact_output_records;
  m.compact_output_pages = a->compact_output_pages;
  m.gc_reused_run_pages = fs->gc_reused_run_pages;
  pmfs_unlock();
  if (capacity > (int)sizeof(m))
    capacity = sizeof(m);
  return copyout(myproc()->pagetable, myproc()->sz, dst, (char *)&m, capacity) <
             0
           ? -1
           : 0;
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

// In ordinary builds this test-only control always returns -1.
uint64
sys_pmfs_trace(void)
{
  int mode, rc;
  argint(0, &mode);
  pmfs_lock();
  rc = pmtrace_control(mode);
  pmfs_unlock();
  return rc;
}
