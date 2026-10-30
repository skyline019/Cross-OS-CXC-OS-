// Test-only implementation of the direct-PMEM publication core. This is NOT
// a mounted filesystem: it holds one synthetic file (at most eight 64-byte
// lines) behind an inode-table node. There is no KV, CDC, block device, or v3
// format. Its bounded first-fit mark/rebuild favors auditable correctness over
// speed. Use only a disposable, dedicated QEMU NVDIMM image.
#ifdef DIRECT_CORE_TEST
#if !defined(PMEM_MAP) || !defined(PMEM_NVDIMM)
#error "DIRECT_CORE_TEST requires a dedicated PMEM_MAP + PMEM_NVDIMM device"
#endif
#ifdef PMFS_SELFTEST
#error "DIRECT_CORE_TEST forbids destructive legacy PMFS selftests"
#endif

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "pmem.h"
#include "pmtrace.h"
#include "directcore.h"

#define DLINE       64U
#define DMAX_DATA   8U
#define DROOT0      0U
#define DROOT1      1U
#define DFIRST      2U
#define DMAGIC      0x31434644U // "DFC1" little endian; experimental only
#define DVERSION    1U
#define DROOT_SALT  0x9b7d4e21U
#define DMAX_MARKS  32
#define DMAX_NEW    12
#define DTYPE_CP    1
#define DTYPE_TABLE 2
#define DTYPE_INODE 3
#define DTYPE_NODE  4
#define DTYPE_DATA  5

// Format layout matches the planned 64-byte checkpoint and inode field order,
// but version DFC1 deliberately restricts the table to one synthetic regular
// file. This test format is not promised to be compatible with the later FS.
struct dcheckpoint {
  uint32 magic;
  uint16 version;
  uint16 flags;
  uint64 epoch;
  uint32 table;
  uint32 table_crc;
  uint8 height;
  uint8 reserved[3];
  uint32 units;
  uint8 reserved2[28];
  uint32 crc;
};

struct dnode {
  uint32 id[8];
  uint32 crc[8];
};

struct dinode {
  uint32 ino;
  uint32 tree;
  uint64 generation;
  uint64 size;
  uint32 nlink;
  uint8 type;
  uint8 height;
  uint16 flags;
  uint32 tree_crc;
  uint32 parent_ino;
  uint64 parent_generation;
  uint64 mtime_ticks;
  uint64 reserved;
};

_Static_assert(sizeof(struct dcheckpoint) == DLINE, "direct checkpoint size");
_Static_assert(sizeof(struct dnode) == DLINE, "direct node size");
_Static_assert(sizeof(struct dinode) == DLINE, "direct inode size");
_Static_assert(__builtin_offsetof(struct dcheckpoint, crc) == 60,
               "direct checkpoint checksum offset");

struct dview {
  uint32 checkpoint;
  uint32 data[DMAX_DATA];
  uint32 size;
  uint64 epoch;
};

struct dmark {
  uint32 id;
  uint32 crc;
  int type;
};

struct dmarks {
  struct dmark entry[DMAX_MARKS];
  int count;
};

struct dlocal {
  uint32 ids[DMAX_MARKS];
  int count;
};

static struct {
  struct sleeplock lock;
  uint8 *base;
  uint32 units;
  int ready;
} direct;

static uint32
crc32_bytes(const void *data, uint32 n)
{
  const uint8 *p = data;
  uint32 crc = ~0U;
  for (uint32 i = 0; i < n; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

static uint64
root_word(uint32 id)
{
  uint32 pair[2] = {id, DROOT_SALT};
  return (uint64)id | ((uint64)crc32_bytes(pair, sizeof(pair)) << 32);
}

static uint8 *
line(uint32 id)
{
  return direct.base + (uint64)id * DLINE;
}

static int
zeros(const void *p, uint32 n)
{
  const uint8 *b = p;
  for (uint32 i = 0; i < n; i++)
    if (b[i])
      return 0;
  return 1;
}

// Record both-root reachability and reject conflicting object identities.
// A second reference from another root is valid; aliasing within one root is
// not emitted by this bounded single-file format and is rejected.
static int
mark(struct dmarks *all, struct dlocal *local, uint32 id, int type, uint32 crc)
{
  if (id < DFIRST || id >= direct.units || local->count >= DMAX_MARKS)
    return -1;
  for (int i = 0; i < local->count; i++)
    if (local->ids[i] == id)
      return -1;
  local->ids[local->count++] = id;
  for (int i = 0; i < all->count; i++) {
    if (all->entry[i].id == id)
      return all->entry[i].type == type && all->entry[i].crc == crc ? 0 : -1;
  }
  if (all->count >= DMAX_MARKS)
    return -1;
  all->entry[all->count++] = (struct dmark){id, crc, type};
  return 0;
}

static int
slot_view(int slot, struct dmarks *all, struct dview *view)
{
  struct dlocal local = {0};
  struct dcheckpoint cp;
  struct dnode table, node;
  struct dinode inode;
  uint64 word = *(volatile uint64 *)line(slot ? DROOT1 : DROOT0);
  uint32 id = (uint32)word;
  uint32 crc, used;

  if (id < DFIRST || id >= direct.units || root_word(id) != word)
    return -1;
  memmove(&cp, line(id), DLINE);
  crc = cp.crc;
  cp.crc = 0;
  if (cp.magic != DMAGIC || cp.version != DVERSION || cp.flags || !cp.epoch ||
      cp.height != 1 || cp.units != direct.units ||
      !zeros(cp.reserved, sizeof(cp.reserved)) ||
      !zeros(cp.reserved2, sizeof(cp.reserved2)) ||
      crc32_bytes(&cp, DLINE) != crc || mark(all, &local, id, DTYPE_CP, crc))
    return -1;
  if (mark(all, &local, cp.table, DTYPE_TABLE, cp.table_crc))
    return -1;
  memmove(&table, line(cp.table), DLINE);
  if (crc32_bytes(&table, DLINE) != cp.table_crc)
    return -1;
  for (int i = 0; i < 8; i++)
    if (i != 1 && (table.id[i] || table.crc[i]))
      return -1;
  if (mark(all, &local, table.id[1], DTYPE_INODE, table.crc[1]))
    return -1;
  memmove(&inode, line(table.id[1]), DLINE);
  if (crc32_bytes(&inode, DLINE) != table.crc[1] || inode.ino != 1 ||
      inode.generation != 1 || inode.size > DIRECTCORE_MAX_SIZE ||
      inode.nlink != 1 || inode.type != 1 || inode.height != 1 || inode.flags ||
      inode.parent_ino || inode.parent_generation || inode.mtime_ticks ||
      inode.reserved)
    return -1;
  used = (inode.size + DLINE - 1) / DLINE;
  memset(view, 0, sizeof(*view));
  view->checkpoint = id;
  view->epoch = cp.epoch;
  view->size = inode.size;
  if (!used)
    return inode.tree == 0 && inode.tree_crc == 0 ? 0 : -1;
  if (mark(all, &local, inode.tree, DTYPE_NODE, inode.tree_crc))
    return -1;
  memmove(&node, line(inode.tree), DLINE);
  if (crc32_bytes(&node, DLINE) != inode.tree_crc)
    return -1;
  for (uint32 i = 0; i < DMAX_DATA; i++) {
    if (i >= used) {
      if (node.id[i] || node.crc[i])
        return -1;
    } else {
      // This experimental format has metadata CRC only, not data CRC.
      if (node.crc[i] || mark(all, &local, node.id[i], DTYPE_DATA, 0))
        return -1;
      view->data[i] = node.id[i];
    }
  }
  return 0;
}

static int
load_roots(struct dmarks *all, struct dview *current, int *active)
{
  struct dview a, b;
  memset(all, 0, sizeof(*all));
  if (!direct.ready || slot_view(0, all, &a) || slot_view(1, all, &b))
    return -1;
  if (a.epoch == b.epoch && a.checkpoint != b.checkpoint)
    return -1;
  *active = a.epoch >= b.epoch ? 0 : 1;
  *current = *active == 0 ? a : b;
  return 0;
}

static int
is_used(struct dmarks *marks, uint32 id, uint32 *reserved, int nreserved)
{
  for (int i = 0; i < marks->count; i++)
    if (marks->entry[i].id == id)
      return 1;
  for (int i = 0; i < nreserved; i++)
    if (reserved[i] == id)
      return 1;
  return 0;
}

static uint32
reserve_line(struct dmarks *marks, uint32 *reserved, int *nreserved)
{
  if (*nreserved >= DMAX_NEW)
    return 0;
  for (uint32 id = DFIRST; id < direct.units; id++) {
    if (!is_used(marks, id, reserved, *nreserved)) {
      reserved[(*nreserved)++] = id;
      return id;
    }
  }
  return 0;
}

static void
store_line(uint32 id, const void *src)
{
  memmove(line(id), (void *)src, DLINE);
  pmtrace_store(line(id), DLINE, 0);
}

static void
store_root(int slot, uint64 word)
{
  volatile uint64 *p = (volatile uint64 *)line(slot ? DROOT1 : DROOT0);
  // Protocol assumption: this naturally aligned 8-byte RV64 store is
  // failure-atomic within one independently persisted 64-byte root line.
  *p = word;
  pmtrace_store((void *)p, sizeof(word), 1);
}

static void
fault_point(int selected, int stage)
{
#ifdef DIRECT_CORE_FAULT_INJECT
  if (selected == stage) {
    printk("DIRECTCORE_FAULT stage=%d\n", stage);
    for (;;)
      asm volatile("" ::: "memory");
  }
#else
  (void)selected;
  (void)stage;
#endif
}

// Explicitly destructive test format. Before publishing either root, persist
// all initial objects; a partial format with only one root fails closed.
static int
format_core(void)
{
  struct dinode inode;
  struct dnode table;
  struct dcheckpoint cp;
  if (!direct.ready || direct.units < 5)
    return -1;
  store_root(0, 0);
  store_root(1, 0);
  pmem_flush_batch_begin();
  pmem_flush_batch(line(DROOT0), 8);
  pmem_flush_batch(line(DROOT1), 8);
  pmem_fence();

  memset(&inode, 0, sizeof(inode));
  inode.ino = 1;
  inode.generation = 1;
  inode.nlink = inode.type = inode.height = 1;
  memset(&table, 0, sizeof(table));
  table.id[1] = 2;
  table.crc[1] = crc32_bytes(&inode, DLINE);
  memset(&cp, 0, sizeof(cp));
  cp.magic = DMAGIC;
  cp.version = DVERSION;
  cp.epoch = 1;
  cp.table = 3;
  cp.table_crc = crc32_bytes(&table, DLINE);
  cp.height = 1;
  cp.units = direct.units;
  cp.crc = crc32_bytes(&cp, DLINE);
  store_line(2, &inode);
  store_line(3, &table);
  store_line(4, &cp);
  pmem_flush_batch_begin();
  pmem_flush_batch(line(2), DLINE);
  pmem_flush_batch(line(3), DLINE);
  pmem_flush_batch(line(4), DLINE);
  pmem_fence();
  store_root(0, root_word(4));
  pmem_flush_batch_begin();
  pmem_flush_batch(line(DROOT0), 8);
  pmem_fence();
  store_root(1, root_word(4));
  pmem_flush_batch_begin();
  pmem_flush_batch(line(DROOT1), 8);
  pmem_fence();
  return 0;
}

static int
read_core(uint8 *data, int capacity)
{
  struct dmarks marks;
  struct dview view;
  int active;
  if (load_roots(&marks, &view, &active) || capacity < (int)view.size)
    return -1;
  for (uint32 off = 0; off < view.size; off += DLINE) {
    uint32 n = view.size - off < DLINE ? view.size - off : DLINE;
    memmove(data + off, line(view.data[off / DLINE]), n);
  }
  return view.size;
}

static int
write_core(const uint8 *data, uint32 size, int stage)
{
  struct dmarks marks;
  struct dview old;
  struct dnode node, table;
  struct dinode inode;
  struct dcheckpoint cp;
  uint8 padded[DIRECTCORE_MAX_SIZE];
  uint32 reserved[DMAX_NEW], ids[DMAX_DATA], node_id = 0;
  uint32 inode_id, table_id, cp_id, used = (size + DLINE - 1) / DLINE;
  int active, count = 0, changed;

  if (size > DIRECTCORE_MAX_SIZE || load_roots(&marks, &old, &active) ||
      old.epoch == ~(uint64)0)
    return -1;
  changed = old.size != size;
  memset(padded, 0, sizeof(padded));
  memmove(padded, (void *)data, size);
  memset(ids, 0, sizeof(ids));
  for (uint32 i = 0; i < used; i++) {
    if (old.data[i] &&
        memcmp(line(old.data[i]), padded + i * DLINE, DLINE) == 0) {
      ids[i] = old.data[i];
    } else {
      changed = 1;
      if (!(ids[i] = reserve_line(&marks, reserved, &count)))
        return -1;
    }
  }
  if (!changed)
    return 0;
  if (used && !(node_id = reserve_line(&marks, reserved, &count)))
    return -1;
  if (!(inode_id = reserve_line(&marks, reserved, &count)) ||
      !(table_id = reserve_line(&marks, reserved, &count)) ||
      !(cp_id = reserve_line(&marks, reserved, &count)))
    return -1; // no persistent stores before all reservations succeed

  for (uint32 i = 0; i < used; i++)
    if (ids[i] != old.data[i])
      store_line(ids[i], padded + i * DLINE);
  memset(&node, 0, sizeof(node));
  for (uint32 i = 0; i < used; i++)
    node.id[i] = ids[i];
  if (used)
    store_line(node_id, &node);
  memset(&inode, 0, sizeof(inode));
  inode.ino = 1;
  inode.tree = node_id;
  inode.generation = 1;
  inode.size = size;
  inode.nlink = inode.type = inode.height = 1;
  inode.tree_crc = used ? crc32_bytes(&node, DLINE) : 0;
  store_line(inode_id, &inode);
  memset(&table, 0, sizeof(table));
  table.id[1] = inode_id;
  table.crc[1] = crc32_bytes(&inode, DLINE);
  store_line(table_id, &table);
  memset(&cp, 0, sizeof(cp));
  cp.magic = DMAGIC;
  cp.version = DVERSION;
  cp.epoch = old.epoch + 1;
  cp.table = table_id;
  cp.table_crc = crc32_bytes(&table, DLINE);
  cp.height = 1;
  cp.units = direct.units;
  cp.crc = crc32_bytes(&cp, DLINE);
  store_line(cp_id, &cp);
  fault_point(stage, 1); // candidates stored, no requested flushes
  pmem_flush_batch_begin();
  for (int i = 0; i < count; i++)
    pmem_flush_batch(line(reserved[i]), DLINE);
  fault_point(stage, 2); // requests issued, data drain not completed
  pmem_fence();
  fault_point(stage, 3); // all new objects drained; root still old
  store_root(1 - active, root_word(cp_id));
  fault_point(stage, 4); // root may be evicted before its explicit flush
  pmem_flush_batch_begin();
  pmem_flush_batch(line(1 - active), 8);
  fault_point(stage, 5); // root flush issued, final drain not completed
  pmem_fence();
  fault_point(stage, 6); // drained, but syscall has not returned
  return 0;
}

void
directcore_init(void)
{
  uint64 bytes;
  initsleeplock(&direct.lock, "directcore");
  if (!pmem_ready() || pmem_size() <= PGSIZE)
    return;
  direct.base = (uint8 *)pmem_base_va() + PGSIZE;
  bytes = pmem_size() - PGSIZE;
  if (((uint64)direct.base & (DLINE - 1)) || bytes / DLINE > 0xffffffffU ||
      bytes / DLINE < 5)
    return;
  direct.units = bytes / DLINE;
#ifdef DIRECT_CORE_LINE_LIMIT
  if (DIRECT_CORE_LINE_LIMIT < 5 || DIRECT_CORE_LINE_LIMIT > direct.units)
    return;
  direct.units = DIRECT_CORE_LINE_LIMIT;
#endif
  direct.ready = 1;
  pmtrace_bind(direct.base, (uint64)direct.units * DLINE);
  printk("DIRECTCORE_READY backend=nvdimm units=%u line=%u experimental=1\n",
         direct.units, DLINE);
}

uint64
sys_directcore(void)
{
  struct proc *p = myproc();
  uint8 data[DIRECTCORE_MAX_SIZE];
  uint64 addr;
  int op, size, stage, rc;
  argint(0, &op);
  argaddr(1, &addr);
  argint(2, &size);
  argint(3, &stage);
  if (!direct.ready || size < 0 || size > DIRECTCORE_MAX_SIZE || stage < 0 ||
      stage > 6)
    return -1;
#ifndef DIRECT_CORE_FAULT_INJECT
  if (stage)
    return -1;
#endif
  if ((op == DIRECTCORE_WRITE || op == DIRECTCORE_TRACE_WRITE) && size &&
      copyin(p->pagetable, p->sz, (char *)data, addr, size) < 0)
    return -1;
  acquiresleep(&direct.lock);
  if (op == DIRECTCORE_FORMAT && size == 0 && stage == 0 &&
      addr == DIRECTCORE_FORMAT_CONFIRM) {
    rc = format_core();
  } else if (op == DIRECTCORE_READ && stage == 0) {
    rc = read_core(data, size);
  } else if (op == DIRECTCORE_WRITE || op == DIRECTCORE_TRACE_WRITE) {
    if (op == DIRECTCORE_TRACE_WRITE && (stage || pmtrace_control(1) != 0)) {
      rc = -1;
    } else {
      rc = write_core(data, size, stage);
      if (op == DIRECTCORE_TRACE_WRITE) {
        if (rc == 0)
          pmtrace_control(2);
        pmtrace_control(0);
      }
    }
  } else {
    rc = -1;
  }
  releasesleep(&direct.lock);
  if (op == DIRECTCORE_READ && rc > 0 &&
      copyout(p->pagetable, p->sz, addr, (char *)data, rc) < 0)
    return -1;
  return rc;
}
#endif // DIRECT_CORE_TEST
