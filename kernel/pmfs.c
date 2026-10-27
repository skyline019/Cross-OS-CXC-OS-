// PMFS durable storage — port of FCS mount/write_sb/dual-slot protocol to xv6 C.
// Reference (read-only): e:\FCS\embdb\x\include\fcs.h read_sb/write_sb/sb_flush
//                         e:\FCS\backup\fcs-from-zero latex crash chapter
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmalgo.h"

// 2 MiB EMUL region so storage logic is testable without QEMU 160M.
// Static: freestanding/MinGW-style kernels dislike large frames.
#define PMFS_EMUL_BYTES (2u * 1024u * 1024u)
static uint8 pmfs_emul_buf[PMFS_EMUL_BYTES];
static struct pmfs g_fs;

enum { PMFS_NIL = 0xffffffffu };

// --- FCS-style checksum (FNV-ish over header with chk zeroed) ---
static uint32
pmfs_sb_chk(const struct pmfs_sb *s)
{
  struct pmfs_sb tmp = *s;
  uint8 *p = (uint8*)&tmp;
  uint32 i;
  uint64 h = 0x9E3779B97F4A7C15ULL;
  tmp.chk = 0;
  for(i = 0; i < PMFS_SB_HDR; i += 8){
    uint64 w = 0;
    memmove(&w, p + i, 8);
    h ^= w;
    h = (h << 7) | (h >> 57);
    h *= 0x100000001B3ULL;
  }
  return (uint32)(h ^ (h >> 32));
}

static void
pmfs_sb_pack(struct pmfs_sb *s, uint64 epoch, uint32 tail)
{
  memset(s, 0, sizeof(*s));
  s->magic = PMFS_MAGIC;
  s->fmt = PMFS_FMT;
  s->state = PMFS_COMMIT_WD;
  s->tail = tail;
  s->man_first = PMFS_NIL;
  s->man_last = PMFS_NIL;
  s->nman = 0;
  s->n_inl = 0;
  s->vers_lo = (uint32)epoch;
  s->vers_hi = (uint32)(epoch >> 32);
  s->chk = pmfs_sb_chk(s);
}

static int
pmfs_sb_ok(const struct pmfs_sb *s)
{
  if(s->magic != PMFS_MAGIC)
    return 0;
  if(s->fmt != PMFS_FMT)
    return 0;
  if(s->state != PMFS_COMMIT_WD)
    return 0;
  if(s->chk != pmfs_sb_chk(s))
    return 0;
  return 1;
}

static uint64
pmfs_sb_epoch(const struct pmfs_sb *s)
{
  return ((uint64)s->vers_hi << 32) | s->vers_lo;
}

// FCS read_sb(slot): load page, validate commit word + checksum.
static int
pmfs_read_sb(struct pmfs *fs, uint32 slot, struct pmfs_sb *out)
{
  struct pmfs_sb s;
  if(slot > 1)
    return -1;
  if(pmdev_read(&fs->dev, slot, 0, &s, PMFS_SB_HDR) != 0)
    return -1;
  if(!pmfs_sb_ok(&s))
    return -1;
  *out = s;
  return 0;
}

// FCS sb_flush: sync → erase/write alternate slot → sync. Slot flips only on success.
static int
pmfs_write_sb_slot(struct pmfs *fs, const struct pmfs_sb *s, uint32 slot)
{
  uint8 page[PMDEV_PAGE];
  memset(page, 0, sizeof(page));
  memmove(page, (void*)s, PMFS_SB_HDR);

  // Barrier 1: data pages already persisted by caller; sync again like FCS.
  pmdev_sync(&fs->dev);

  // "Erase" slot (zero) then write full page — CRC/state make tear-safe.
  memset(pmdev_raw(&fs->dev, slot), 0, PMDEV_PAGE);
  pmem_persist(pmdev_raw(&fs->dev, slot), PMDEV_PAGE);

  if(pmdev_write_page(&fs->dev, slot, page) != 0)
    return -1;

  pmdev_sync(&fs->dev);
  return 0;
}

struct pmfs*
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
  if(fs == 0 || fs->dev.base == 0)
    return -1;
  memset(fs->dev.base, 0, fs->dev.nbytes);
  pmem_persist(fs->dev.base, fs->dev.nbytes);

  fs->epoch = 1;
  fs->tail = PMFS_DATA0;
  fs->sb_slot = PMFS_SLOT_B; // first durable write lands on A (slot^1)

  pmfs_sb_pack(&sb, fs->epoch, fs->tail);
  if(pmfs_write_sb_slot(fs, &sb, PMFS_SLOT_A) != 0)
    return -1;
  fs->sb_slot = PMFS_SLOT_A;

  // Second SB so both slots valid (FCS mount formats with two write_sb).
  fs->epoch = 2;
  pmfs_sb_pack(&sb, fs->epoch, fs->tail);
  if(pmfs_write_sb_slot(fs, &sb, PMFS_SLOT_B) != 0)
    return -1;
  fs->sb_slot = PMFS_SLOT_B;
  fs->mounted = 1;
  return 0;
}

int
pmfs_mount(struct pmfs *fs)
{
  struct pmfs_sb a, b;
  int oa, ob;
  struct pmfs_sb *best;

  if(fs == 0 || fs->dev.base == 0)
    return -1;

  oa = pmfs_read_sb(fs, PMFS_SLOT_A, &a) == 0;
  ob = pmfs_read_sb(fs, PMFS_SLOT_B, &b) == 0;

  if(!oa && !ob){
    printk("pmfs: no valid SB — formatting\n");
    return pmfs_format(fs);
  }

  if(oa && ob)
    best = (pmfs_sb_epoch(&b) >= pmfs_sb_epoch(&a)) ? &b : &a;
  else
    best = oa ? &a : &b;

  fs->sb_slot = (best == &a) ? PMFS_SLOT_A : PMFS_SLOT_B;
  fs->epoch = pmfs_sb_epoch(best);
  fs->tail = best->tail;
  if(fs->tail < PMFS_DATA0 || fs->tail > fs->npages)
    fs->tail = PMFS_DATA0;
  fs->mounted = 1;
  printk("pmfs: mount ok epoch=%d slot=%d tail=%d pages=%d%s\n",
         (int)fs->epoch, fs->sb_slot, fs->tail, fs->npages,
         fs->emul ? " [EMUL]" : "");
  return 0;
}

int
pmfs_commit(struct pmfs *fs)
{
  struct pmfs_sb sb;
  uint32 slot;

  if(fs == 0 || !fs->mounted)
    return -1;

  fs->epoch++;
  pmfs_sb_pack(&sb, fs->epoch, fs->tail);
  slot = fs->sb_slot ^ 1u;   // FCS: alternate every durable SB write
  if(pmfs_write_sb_slot(fs, &sb, slot) != 0)
    return -1;
  fs->sb_slot = slot;
  return 0;
}

int
pmfs_alloc_pages(struct pmfs *fs, uint32 n, uint32 *out_pid)
{
  uint32 pid;
  if(fs == 0 || !fs->mounted || n == 0 || out_pid == 0)
    return -1;
  if((uint64)fs->tail + n > fs->npages)
    return -1;
  pid = fs->tail;
  fs->tail += n;
  *out_pid = pid;
  return 0;
}

int
pmfs_write_data_page(struct pmfs *fs, uint32 pid, const void *src)
{
  if(fs == 0 || !fs->mounted || pid < PMFS_DATA0 || src == 0)
    return -1;
  return pmdev_write_page(&fs->dev, pid, src);
}

int
pmfs_storage_selftest(void)
{
  struct pmfs *fs = &g_fs;
  uint32 pid = 0;
  uint8 page[PMDEV_PAGE];
  uint8 back[PMDEV_PAGE];
  uint64 ep_before;
  uint32 slot_before;

  if(!fs->mounted)
    return -1;

  memset(page, 0x5a, sizeof(page));
  page[0] = 'P'; page[1] = 'M'; page[2] = 'T'; page[3] = '0';

  if(pmfs_alloc_pages(fs, 1, &pid) != 0)
    return -2;
  if(pmfs_write_data_page(fs, pid, page) != 0)
    return -3;

  ep_before = fs->epoch;
  slot_before = fs->sb_slot;
  if(pmfs_commit(fs) != 0)
    return -4;
  if(fs->epoch != ep_before + 1)
    return -5;
  if(fs->sb_slot == slot_before)
    return -6; // must flip slot

  // Simulate remount (drop RAM state, reload SBs).
  fs->mounted = 0;
  fs->epoch = 0;
  fs->tail = 0;
  if(pmfs_mount(fs) != 0)
    return -7;
  if(fs->epoch != ep_before + 1)
    return -8;
  if(fs->tail != pid + 1)
    return -9;

  if(pmdev_read(&fs->dev, pid, 0, back, PMDEV_PAGE) != 0)
    return -10;
  if(back[0] != 'P' || back[1] != 'M' || back[2] != 'T' || back[3] != '0')
    return -11;

  // Corrupt inactive slot; mount must still succeed (FCS dual-slot).
  {
    uint32 bad = fs->sb_slot ^ 1u;
    memset(pmdev_raw(&fs->dev, bad), 0xee, PMDEV_PAGE);
    pmem_persist(pmdev_raw(&fs->dev, bad), PMDEV_PAGE);
  }
  fs->mounted = 0;
  if(pmfs_mount(fs) != 0)
    return -12;

  printk("pmfs: STORAGE_SELFTEST_PASS epoch=%d pid=%d\n",
         (int)fs->epoch, pid);
  return 0;
}

void
pmfs_init(void)
{
  void *base = 0;
  uint64 nbytes = 0;
  int emul = 0;

  memset(&g_fs, 0, sizeof(g_fs));

  if(pmem_ready()){
    base = pmem_base_va();
    nbytes = pmem_size();
    emul = 0;
  } else {
    // FCS RamDev stand-in until QEMU NVDIMM window is wired (M1).
    base = pmfs_emul_buf;
    nbytes = PMFS_EMUL_BYTES;
    emul = 1;
    printk("pmfs: using EMUL buffer (%d KiB) — FCS RamDev analogue\n",
           (int)(PMFS_EMUL_BYTES / 1024));
  }

  if(pmdev_open(&g_fs.dev, base, nbytes) != 0){
    printk("pmfs: pmdev_open failed\n");
    return;
  }
  g_fs.emul = emul;
  g_fs.npages = pmdev_capacity(&g_fs.dev);

  if(pmfs_mount(&g_fs) != 0){
    printk("pmfs: mount/format failed\n");
    return;
  }

  if(pmfs_storage_selftest() != 0)
    printk("pmfs: STORAGE_SELFTEST_FAIL\n");

  // FCS algorithm layer: mem→run, bloom/fence get, B1/B2 maintain, WA meters.
  pm_algo_init(pm_algo_global(), &g_fs);
  if(pm_algo_selftest() != 0)
    printk("pmfs: ALGO_SELFTEST_FAIL\n");
}
