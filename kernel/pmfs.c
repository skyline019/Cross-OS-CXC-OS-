// PMFS durable storage — port of FCS mount/write_sb/dual-slot protocol to xv6 C.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "pmem.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmalgo.h"
#include "pmindex.h"
#include "pmfile.h"
#include "pmcdc.h"
#include "pmstream.h"
#include "pmcontract.h"

#define PMFS_EMUL_BYTES                                                        \
  (8u * 1024u * 1024u) // headroom for contract + N-sweep seals
static uint8 pmfs_emul_buf[PMFS_EMUL_BYTES];
static struct pmfs g_fs;
static struct sleeplock pmfs_api_lock;
typedef char
  pmfs_sb_size_check[(sizeof(struct pmfs_sb) == PMFS_SB_HDR) ? 1 : -1];
typedef char pmfs_sb_state_offset_check
  [(__builtin_offsetof(struct pmfs_sb, state) == 8) ? 1 : -1];
typedef char pmfs_sb_checksum_offset_check
  [(__builtin_offsetof(struct pmfs_sb, chk) == 12) ? 1 : -1];

#ifdef PMFS_FAULT_INJECT
static int fault_stage, fault_skip, fault_active;

int
pmfs_fault_arm(int stage, int skip)
{
  if (!g_fs.mounted || stage < 1 || stage > 7 || skip < 0 || skip > 16)
    return -1;
  fault_stage = stage;
  fault_skip = skip;
  return 0;
}

static void
fault_begin(void)
{
  fault_active = 0;
  if (fault_stage && fault_stage <= 5) {
    if (fault_skip)
      fault_skip--;
    else
      fault_active = 1;
  }
}

static void
fault_point(int stage)
{
  if (fault_active && fault_stage == stage) {
    printk("PMFS_FAULT_POINT stage=%d\n", stage);
    // Test harness kills QEMU at this exact software boundary. This is not a
    // hardware power-failure or persistence-domain simulation.
    for (;;)
      asm volatile("" ::: "memory");
  }
}
#else
int
pmfs_fault_arm(int stage, int skip)
{
  (void)stage;
  (void)skip;
  return -1;
}
#define fault_begin()      ((void)0)
#define fault_point(stage) ((void)0)
#endif

static uint32
pmfs_sb_chk(const struct pmfs_sb *s)
{
  struct pmfs_sb tmp = *s;
  uint8 *p = (uint8 *)&tmp;
  uint32 i;
  uint64 h = 0x9E3779B97F4A7C15ULL;
  tmp.chk = 0;
  for (i = 0; i < PMFS_SB_HDR; i += 8) {
    uint64 w = 0;
    memmove(&w, p + i, 8);
    h ^= w;
    h = (h << 7) | (h >> 57);
    h *= 0x100000001B3ULL;
  }
  return (uint32)(h ^ (h >> 32));
}

static void
pmfs_sb_pack(struct pmfs_sb *s, struct pmfs *fs, uint64 epoch)
{
  memset(s, 0, sizeof(*s));
  s->magic = PMFS_MAGIC;
  s->fmt = PMFS_FMT;
  s->state = PMFS_COMMIT_WD;
  s->tail = fs->tail;
  s->inode_page = fs->inode_page;
  s->dent_page = fs->dent_page;
  s->run_head = fs->run_head;
  s->vers_lo = (uint32)epoch;
  s->vers_hi = (uint32)(epoch >> 32);
  s->chk = pmfs_sb_chk(s);
}

static int
pmfs_sb_ok(const struct pmfs_sb *s)
{
  if (s->magic != PMFS_MAGIC)
    return 0;
  if (s->fmt != PMFS_FMT)
    return 0;
  if (s->state != PMFS_COMMIT_WD)
    return 0;
  if (s->chk != pmfs_sb_chk(s))
    return 0;
  return 1;
}

static uint64
pmfs_sb_epoch(const struct pmfs_sb *s)
{
  return ((uint64)s->vers_hi << 32) | s->vers_lo;
}

// Reject out-of-range roots and allocator tails instead of silently resetting.
static int
pmfs_read_sb(struct pmfs *fs, uint32 slot, struct pmfs_sb *out)
{
  struct pmfs_sb s;
  if (slot > 1 || pmdev_read(&fs->dev, slot, 0, &s, PMFS_SB_HDR) != 0)
    return -1;
  if (!pmfs_sb_ok(&s) || s.tail < PMFS_DATA0 || s.tail > fs->npages)
    return -1;
  if ((s.inode_page == PMFS_NIL) != (s.dent_page == PMFS_NIL))
    return -1;
  if (s.inode_page != PMFS_NIL &&
      (s.inode_page < PMFS_DATA0 || s.inode_page >= s.tail ||
       s.dent_page < PMFS_DATA0 || s.dent_page >= s.tail ||
       s.inode_page == s.dent_page))
    return -1;
  if (s.run_head != PMFS_NIL &&
      (s.run_head < PMFS_DATA0 || s.run_head >= s.tail))
    return -1;
  *out = s;
  return 0;
}

// Publish one checkpoint without overwriting the currently active slot.
// The aligned 8-byte state/checksum word is the final store. This is a
// protocol under an explicitly stated atomic-store/persistence-domain model,
// not a claim that the default QEMU RAM backend survives hardware power loss.
static int
pmfs_write_sb_slot(struct pmfs *fs, const struct pmfs_sb *s, uint32 slot)
{
  struct pmfs_sb staged = *s;
  uint8 *raw = pmdev_raw(&fs->dev, slot);
  volatile uint64 *word;
  uint64 commit;

  if (raw == 0 || ((uint64)(raw + 8) & 7) != 0)
    return -1;
  word = (volatile uint64 *)(raw + 8);
  *word = 0;
  pmem_persist((void *)word, sizeof(*word));
  fault_point(2);

  staged.state = 0;
  staged.chk = 0;
  if (pmdev_write(&fs->dev, slot, 0, &staged, PMFS_SB_HDR) != 0)
    return -1;
  pmem_persist(raw, PMFS_SB_HDR);
  fault_point(3);

  commit = ((uint64)s->chk << 32) | s->state;
  *word = commit;
  fault_point(4);
  pmem_persist((void *)word, sizeof(*word));
  fault_point(5);
  return 0;
}

struct pmfs *
pmfs_store(void)
{
  return &g_fs;
}

int
pmfs_mounted(void)
{
  return g_fs.mounted;
}

int
pmfs_format(struct pmfs *fs)
{
  struct pmfs_sb sb;
  if (fs == 0 || fs->dev.base == 0 || fs->npages <= PMFS_DATA0)
    return -1;
  // Explicitly destructive. Never call on a failed persistent mount.
  memset(fs->dev.base, 0, fs->dev.nbytes);
  pmem_persist(fs->dev.base, fs->dev.nbytes);
  fs->tail = PMFS_DATA0;
  fs->inode_page = PMFS_NIL;
  fs->dent_page = PMFS_NIL;
  fs->run_head = PMFS_NIL;
  fs->epoch = 1;
  fs->sb_slot = PMFS_SLOT_A;
  pmfs_sb_pack(&sb, fs, fs->epoch);
  if (pmfs_write_sb_slot(fs, &sb, PMFS_SLOT_A) != 0)
    return -1;
  fs->epoch = 2;
  fs->sb_slot = PMFS_SLOT_B;
  pmfs_sb_pack(&sb, fs, fs->epoch);
  if (pmfs_write_sb_slot(fs, &sb, PMFS_SLOT_B) != 0)
    return -1;
  fs->mounted = 1;
  return 0;
}

int
pmfs_mount(struct pmfs *fs)
{
  struct pmfs_sb a, b;
  int oa, ob;
  struct pmfs_sb *best;

  if (fs == 0 || fs->dev.base == 0)
    return -1;
  oa = pmfs_read_sb(fs, PMFS_SLOT_A, &a) == 0;
  ob = pmfs_read_sb(fs, PMFS_SLOT_B, &b) == 0;
  if (!oa && !ob) {
    // Unformatted, incompatible, and corrupted media are all fail-closed.
    printk("pmfs: no valid v3 checkpoint; explicit format required\n");
    fs->mounted = 0;
    return -1;
  }
  if (oa && ob)
    best = (pmfs_sb_epoch(&b) >= pmfs_sb_epoch(&a)) ? &b : &a;
  else
    best = oa ? &a : &b;
  fs->sb_slot = (best == &a) ? PMFS_SLOT_A : PMFS_SLOT_B;
  fs->epoch = pmfs_sb_epoch(best);
  fs->tail = best->tail;
  fs->inode_page = best->inode_page;
  fs->dent_page = best->dent_page;
  fs->run_head = best->run_head;
  fs->mounted = 1;
  printk("pmfs: mount ok epoch=%d slot=%d tail=%d pages=%d%s\n", (int)fs->epoch,
         fs->sb_slot, fs->tail, fs->npages, fs->emul ? " [EMUL]" : "");
  return 0;
}

int
pmfs_commit(struct pmfs *fs)
{
  struct pmfs_sb sb;
  uint32 slot;
  uint64 next_epoch;

  if (fs == 0 || !fs->mounted || fs->epoch == ~(uint64)0)
    return -1;
  next_epoch = fs->epoch + 1;
  pmfs_sb_pack(&sb, fs, next_epoch);
  slot = fs->sb_slot ^ 1u;
  fault_begin();
  fault_point(1);
  if (pmfs_write_sb_slot(fs, &sb, slot) != 0)
    return -1;
  fs->epoch = next_epoch;
  fs->sb_slot = slot;
  return 0;
}

int
pmfs_alloc_pages(struct pmfs *fs, uint32 n, uint32 *out_pid)
{
  uint32 pid;
  if (fs == 0 || !fs->mounted || n == 0 || out_pid == 0 ||
      fs->tail > fs->npages || n > fs->npages - fs->tail)
    return -1;
  pid = fs->tail;
  fs->tail += n;
  *out_pid = pid;
  return 0;
}

int
pmfs_write_data_page(struct pmfs *fs, uint32 pid, const void *src)
{
  if (fs == 0 || !fs->mounted || pid < PMFS_DATA0 || src == 0)
    return -1;
#ifdef PMFS_FAULT_INJECT
  if (fault_stage >= 6) {
    if (fault_skip) {
      fault_skip--;
    } else {
      int rc;
      fault_active = 1;
      if (fault_stage == 6) {
        // Model a partially written new page before it can become reachable.
        if (pmdev_write(&fs->dev, pid, 0, src, PMDEV_PAGE / 2) != 0)
          return -1;
        pmem_persist(pmdev_raw(&fs->dev, pid), PMDEV_PAGE / 2);
        fault_point(6);
      }
      rc = pmdev_write_page(&fs->dev, pid, src);
      if (rc == 0)
        fault_point(7);
      return rc;
    }
  }
#endif
  return pmdev_write_page(&fs->dev, pid, src);
}

int
pmfs_storage_selftest(void)
{
  struct pmfs *fs = &g_fs;
  uint32 pid = 0;
  // Must be static: main() runs on 4KiB stack0; 2×4KiB locals overflow.
  static uint8 page[PMDEV_PAGE];
  static uint8 back[PMDEV_PAGE];
  uint64 ep_before;
  uint32 slot_before;
  int code;

  if (!fs->mounted) {
    code = 1;
    goto fail;
  }

  memset(page, 0x5a, sizeof(page));
  page[0] = 'P';
  page[1] = 'M';
  page[2] = 'T';
  page[3] = '0';

  if (pmfs_alloc_pages(fs, 1, &pid) != 0) {
    code = 2;
    goto fail;
  }
  if (pmfs_write_data_page(fs, pid, page) != 0) {
    code = 3;
    goto fail;
  }

  ep_before = fs->epoch;
  slot_before = fs->sb_slot;
  if (pmfs_commit(fs) != 0) {
    code = 4;
    goto fail;
  }
  if (fs->epoch != ep_before + 1) {
    code = 5;
    goto fail;
  }
  if (fs->sb_slot == slot_before) {
    code = 6;
    goto fail;
  }

  fs->mounted = 0;
  fs->epoch = 0;
  fs->tail = 0;
  if (pmfs_mount(fs) != 0) {
    code = 7;
    goto fail;
  }
  if (fs->epoch != ep_before + 1) {
    code = 8;
    goto fail;
  }
  if (fs->tail != pid + 1) {
    code = 9;
    goto fail;
  }

  if (pmdev_read(&fs->dev, pid, 0, back, PMDEV_PAGE) != 0) {
    code = 10;
    goto fail;
  }
  if (back[0] != 'P' || back[1] != 'M' || back[2] != 'T' || back[3] != '0') {
    code = 11;
    goto fail;
  }

  {
    uint32 bad = fs->sb_slot ^ 1u;
    memset(pmdev_raw(&fs->dev, bad), 0xee, PMDEV_PAGE);
    pmem_persist(pmdev_raw(&fs->dev, bad), PMDEV_PAGE);
  }
  fs->mounted = 0;
  if (pmfs_mount(fs) != 0) {
    code = 12;
    goto fail;
  }

  // Restore dual-slot health so CRASH can tear the active slot safely.
  if (pmfs_commit(fs) != 0) {
    code = 12;
    goto fail;
  }

  printk("ONBOX_PMFS_STORE_PASS epoch=%d pid=%d\n", (int)fs->epoch, pid);
  pmcontract_mark("STORE", 1);
  return 0;

fail:
  pmcontract_fail("STORE", code);
  return -code;
}

// CRASH: tear active slot must still mount via other; uncommitted not visible.
int
pmfs_crash_selftest(void)
{
  struct pmfs *fs = &g_fs;
  uint32 pid = 0;
  static uint8 page[PMDEV_PAGE];
  uint64 ep0, ep1;
  uint32 slot_act;
  uint32 tail0;

  if (!fs->mounted) {
    pmcontract_fail("CRASH", 1);
    return -1;
  }

  // Make both slots refer to the same roots before tearing a slot. Otherwise
  // this destructive selftest could roll back a run still present in RAM.
  if (pmfs_commit(fs) != 0) {
    pmcontract_fail("CRASH", 1);
    return -1;
  }
  // --- 1) Tear the *active* (latest) slot; mount must use the other ---
  ep0 = fs->epoch;
  slot_act = fs->sb_slot;
  memset(pmdev_raw(&fs->dev, slot_act), 0xcc, PMDEV_PAGE);
  pmem_persist(pmdev_raw(&fs->dev, slot_act), PMDEV_PAGE);
  fs->mounted = 0;
  if (pmfs_mount(fs) != 0) {
    // Both bad? Should not happen — other slot still valid.
    pmcontract_fail("CRASH", 1);
    return -1;
  }
  if (fs->sb_slot == slot_act) {
    // Mounted the torn slot somehow
    pmcontract_fail("CRASH", 1);
    return -1;
  }
  if (fs->epoch > ep0) {
    pmcontract_fail("CRASH", 1);
    return -1;
  }
  // Restore a clean dual-slot state for further tests.
  if (pmfs_commit(fs) != 0) {
    pmcontract_fail("CRASH", 1);
    return -1;
  }

  // --- 2) Uncommitted write must not advance durable tail ---
  ep1 = fs->epoch;
  tail0 = fs->tail;
  memset(page, 0x11, sizeof(page));
  page[0] = 'U';
  page[1] = 'N';
  page[2] = 'C';
  page[3] = 'M';
  if (pmfs_alloc_pages(fs, 1, &pid) != 0) {
    pmcontract_fail("CRASH", 2);
    return -1;
  }
  if (pmfs_write_data_page(fs, pid, page) != 0) {
    pmcontract_fail("CRASH", 2);
    return -1;
  }
  // Intentionally NO commit — remount must restore old tail/epoch.
  fs->mounted = 0;
  fs->epoch = 0;
  fs->tail = 0;
  if (pmfs_mount(fs) != 0) {
    pmcontract_fail("CRASH", 2);
    return -1;
  }
  if (fs->epoch != ep1 || fs->tail != tail0) {
    pmcontract_fail("CRASH", 2);
    return -1;
  }

  printk("ONBOX_PMFS_CRASH_PASS epoch=%d\n", (int)fs->epoch);
  pmcontract_mark("CRASH", 1);
  return 0;
}

// Global prototype state, scratch buffers, streams and metadata are protected
// together; no caller may hold this lock across a user-visible sleep.
void
pmfs_lock(void)
{
  acquiresleep(&pmfs_api_lock);
}

void
pmfs_unlock(void)
{
  releasesleep(&pmfs_api_lock);
}

// Explicit destructive action, only available when no PMFS is mounted.
int
pmfs_format_unmounted(void)
{
  if (g_fs.mounted || g_fs.dev.base == 0)
    return -1;
  if (pmfs_format(&g_fs) != 0)
    return -1;
  pm_algo_init(pm_algo_global(), &g_fs);
  if (pmfile_format() != 0 || pmfs_commit(&g_fs) != 0) {
    g_fs.mounted = 0;
    return -1;
  }
  printk("pmfs: explicitly formatted v3\n");
  return 0;
}

void
pmfs_init(void)
{
  void *base;
  uint64 nbytes;
  int emul;
#ifdef PMFS_SELFTEST
  int rc;
#endif

  initsleeplock(&pmfs_api_lock, "pmfs");
  memset(&g_fs, 0, sizeof(g_fs));
#ifdef PMEM_MAP
  if (!pmem_ready()) {
    printk("pmfs: MAP unavailable; refusing volatile fallback\n");
    return;
  }
#endif
  if (pmem_ready()) {
    if (pmem_size() <= PGSIZE) {
      printk("pmfs: MAP window overlaps firmware data; refusing mount\n");
      return;
    }
    base = (void *)((uint64)pmem_base_va() + PGSIZE);
    nbytes = pmem_size() - PGSIZE;
    emul = 0;
    printk("ONBOX_PMFS_PMEM0_PASS mode=MAP backend=%s size=%d\n",
           pmem_backend_name(), (int)nbytes);
#ifdef PMFS_SELFTEST
    pmcontract_mark("PMEM0", 1);
    pmem_contract_map();
#endif
  } else {
    base = pmfs_emul_buf;
    nbytes = PMFS_EMUL_BYTES;
    emul = 1;
    printk("ONBOX_PMFS_PMEM0_PASS mode=EMUL backend=emul size=%d\n",
           (int)nbytes);
#ifdef PMFS_SELFTEST
    pmcontract_mark("PMEM0", 1);
#endif
  }
  if (pmdev_open(&g_fs.dev, base, nbytes) != 0) {
    printk("pmfs: device unavailable\n");
    return;
  }
  g_fs.emul = emul;
  g_fs.npages = pmdev_capacity(&g_fs.dev);

  if (emul) {
    // The volatile in-kernel array has no prior persistent contents.
    if (pmfs_format_unmounted() != 0) {
      printk("pmfs: volatile format failed\n");
      return;
    }
  } else if (pmfs_mount(&g_fs) != 0) {
#ifdef PMFS_SELFTEST
    // Destructive legacy tests are opt-in and must use a disposable image.
    if (pmfs_format_unmounted() != 0)
      return;
#else
    return;
#endif
  } else {
    pm_algo_init(pm_algo_global(), &g_fs);
    if (pm_algo_recover(pm_algo_global()) != 0 || pmfile_init() != 0) {
      printk("pmfs: corrupt v3 data or metadata; mount refused (no writes)\n");
      g_fs.mounted = 0;
      return;
    }
  }
  printk("ONBOX_PMFS_MOUNT_PASS epoch=%d slot=%d emul=%d\n", (int)g_fs.epoch,
         g_fs.sb_slot, emul);
#ifdef PMFS_SELFTEST
  pmcontract_mark("MOUNT", 1);
  rc = pmfs_storage_selftest();
  if (rc != 0)
    ; // already FAIL-lined

  rc = pmindex_selftest();
  if (rc != 0)
    ; // already FAIL-lined

  rc = pm_algo_selftest();
  if (rc != 0)
    ;

  // Prove index is rebuildable from sealed runs (Aurora remount path).
  if (pm_algo_rebuild_index(pm_algo_global()) != 0)
    pmcontract_fail("INDEX", 20);

  rc = pmfs_crash_selftest();
  if (rc != 0)
    ;

  rc = pmem_persist_selftest();
  if (rc != 0)
    ;

  rc = pmfile_selftest();
  if (rc != 0)
    ;

  rc = pmcdc_selftest();
  if (rc != 0)
    ;

  rc = pmstream_selftest();
  if (rc != 0)
    ;

  rc = pmfile_chaos_selftest();
  if (rc != 0)
    ;

  rc = pmfile_edge_selftest();
  if (rc != 0)
    ;

  rc = pmfile_ablation_selftest();
  if (rc != 0)
    ;

  rc = pmfile_remount_selftest();
  if (rc != 0)
    ;

  rc = pmfile_fault_selftest();
  if (rc != 0)
    ;

  pmcontract_gate();
#else
  printk("pmfs: ready (v3, boot selftests disabled)\n");
#endif
}
