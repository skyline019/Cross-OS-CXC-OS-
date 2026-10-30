// Minimal, polling PCIe NVMe root-disk backend for QEMU's RISC-V virt machine.
//
// Deliberate scope: bus 0, exactly one controller, namespace 1, one admin
// queue and one serialized I/O queue, 4 KiB host pages, 512/1024-byte LBAs,
// and no namespace metadata. DMA assumes xv6's identity-mapped kernel memory.
// This is an experimental block-device baseline, NOT the PMEM path and NOT a
// general PCI/NVMe driver. Errors and timeouts fail closed via panic.
#ifdef XV6_DISK_NVME
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define NVME_QDEPTH 16
#define NVME_NSID   1

// QEMU virt's timer frequency is 10 MHz. This bound is specific to this
// experimental machine; a general driver must discover the timebase.
#define NVME_TIMEOUT_TICKS 300000000ULL

#define NVME_CAP   0x00
#define NVME_INTMS 0x0c
#define NVME_CC    0x14
#define NVME_CSTS  0x1c
#define NVME_AQA   0x24
#define NVME_ASQ   0x28
#define NVME_ACQ   0x30
#define NVME_DB    0x1000

#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY  0x06
#define NVME_IO_FLUSH        0x00
#define NVME_IO_WRITE        0x01
#define NVME_IO_READ         0x02

struct nvme_command {
  uint8 opcode;
  uint8 flags;
  uint16 cid;
  uint32 nsid;
  uint64 reserved2;
  uint64 mptr;
  uint64 prp1;
  uint64 prp2;
  uint32 cdw10;
  uint32 cdw11;
  uint32 cdw12;
  uint32 cdw13;
  uint32 cdw14;
  uint32 cdw15;
};

struct nvme_completion {
  uint32 result;
  uint32 reserved;
  uint16 sq_head;
  uint16 sq_id;
  uint16 cid;
  uint16 status;
};

_Static_assert(sizeof(struct nvme_command) == 64, "NVMe SQ entry size");
_Static_assert(sizeof(struct nvme_completion) == 16, "NVMe CQ entry size");
_Static_assert(__builtin_offsetof(struct buf, data) % 4 == 0,
               "NVMe PRP data must be DWORD-aligned");

struct nvme_queue {
  struct nvme_command *sq;
  volatile struct nvme_completion *cq;
  uint16 qid;
  uint16 depth;
  uint16 sq_tail;
  uint16 cq_head;
  uint16 phase;
  uint16 next_cid;
};

static struct nvme_command admin_sq[NVME_QDEPTH]
  __attribute__((aligned(PGSIZE)));
static struct nvme_completion admin_cq[NVME_QDEPTH]
  __attribute__((aligned(PGSIZE)));
static struct nvme_command io_sq[NVME_QDEPTH] __attribute__((aligned(PGSIZE)));
static struct nvme_completion io_cq[NVME_QDEPTH]
  __attribute__((aligned(PGSIZE)));
static uint8 identify_data[PGSIZE] __attribute__((aligned(PGSIZE)));

static struct {
  struct spinlock lock;
  struct nvme_queue admin;
  struct nvme_queue io;
  uint32 db_stride;
  uint32 lba_shift;
  uint64 bar_size;
  uint64 ns_blocks;
} nvme;

static volatile uint32 *
reg32(uint32 off)
{
  return (volatile uint32 *)(PCIE_MMIO + off);
}

static volatile uint64 *
reg64(uint32 off)
{
  return (volatile uint64 *)(PCIE_MMIO + off);
}

static void
wait_ready(int ready)
{
  uint64 start = r_time();
  for (;;) {
    uint32 status = *reg32(NVME_CSTS);
    if (status & 2)
      panic("nvme controller fatal");
    if ((status & 1) == (uint32)ready)
      return;
    if (r_time() - start > NVME_TIMEOUT_TICKS)
      panic("nvme controller timeout");
  }
}

// PCI config space and BAR mapping are deliberately confined to QEMU virt,
// bus 0 and a single controller. Reject an unsupported layout rather than
// accidentally reprogramming other devices or overlapping another BAR.
static void
pci_enable_nvme(void)
{
  volatile uint32 *found = 0;
  int found_slot = -1, found_func = -1, count = 0;
  for (int slot = 0; slot < 32; slot++) {
    for (int func = 0; func < 8; func++) {
      volatile uint32 *cfg =
        (volatile uint32 *)(PCIE_ECAM + ((uint64)slot << 15) +
                            ((uint64)func << 12));
      if ((cfg[0] & 0xffff) == 0xffff)
        continue;
      if ((cfg[2] >> 8) == 0x010802) {
        found = cfg;
        found_slot = slot;
        found_func = func;
        count++;
      }
    }
  }
  if (count != 1)
    panic("nvme requires exactly one PCI controller on bus 0");

  volatile uint16 *command = (volatile uint16 *)((uint64)found + 4);
  uint16 original_command = *command;
  *command =
    original_command & ~(uint16)6; // disable memory and DMA to size BAR
  io_fence();

  uint32 bar_type = found[4];
  if ((bar_type & 7) != 4) // 64-bit memory BAR only
    panic("nvme unsupported PCI BAR0");
  found[4] = 0xffffffff;
  found[5] = 0xffffffff;
  io_fence();
  uint64 mask = ((uint64)found[5] << 32) | (found[4] & ~0xfU);
  uint64 size = ~mask + 1;
  if (!size || (size & (size - 1)) || size > PCIE_MMIO_SIZE ||
      (PCIE_MMIO & (size - 1)))
    panic("nvme BAR0 outside reserved window");
  nvme.bar_size = size;
  found[5] = 0;
  found[4] = (uint32)PCIE_MMIO;
  io_fence();
  // Enable memory and bus mastering; disable legacy INTx (queues poll).
  *command = original_command | 6 | (1 << 10);
  io_fence();
  printk("nvme: PCI bus=0 slot=%d function=%d BAR0=%lx size=%lu\n", found_slot,
         found_func, (uint64)PCIE_MMIO, size);
}

static uint32
submit(struct nvme_queue *q, struct nvme_command *cmd)
{
  uint16 cid = q->next_cid++;
  cmd->cid = cid;
  memmove(&q->sq[q->sq_tail], cmd, sizeof(*cmd));
  q->sq_tail = (q->sq_tail + 1) % q->depth;
  io_fence(); // DMA descriptors before the SQ tail doorbell
  *reg32(NVME_DB + (2 * q->qid) * nvme.db_stride) = q->sq_tail;

  volatile struct nvme_completion *c = &q->cq[q->cq_head];
  uint64 start = r_time();
  while ((c->status & 1) != q->phase) {
    if (*reg32(NVME_CSTS) & 2)
      panic("nvme controller fatal during I/O");
    if (r_time() - start > NVME_TIMEOUT_TICKS)
      panic("nvme command timeout");
  }
  io_fence(); // completion phase before the other fields and DMA data
  uint16 status = c->status;
  uint16 completed_cid = c->cid;
  uint16 completed_sq = c->sq_id;
  uint32 result = c->result;
  if (completed_cid != cid || completed_sq != q->qid || (status >> 1) != 0) {
    printk("nvme: completion q=%d cid=%d gotq=%d gotcid=%d status=%x\n", q->qid,
           cid, completed_sq, completed_cid, status);
    panic("nvme command failed");
  }
  q->cq_head++;
  if (q->cq_head == q->depth) {
    q->cq_head = 0;
    q->phase ^= 1;
  }
  io_fence();
  *reg32(NVME_DB + (2 * q->qid + 1) * nvme.db_stride) = q->cq_head;
  return result;
}

static void
identify(uint32 cns, uint32 nsid)
{
  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  memset(identify_data, 0, sizeof(identify_data));
  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = nsid;
  cmd.prp1 = (uint64)identify_data;
  cmd.cdw10 = cns;
  submit(&nvme.admin, &cmd);
}

void
nvme_disk_init(void)
{
  struct nvme_command cmd;
  initlock(&nvme.lock, "nvme");
  pci_enable_nvme();

  uint64 cap = *reg64(NVME_CAP);
  uint32 max_entries = (cap & 0xffff) + 1;
  uint32 dstrd = (cap >> 32) & 0xf;
  if (((cap >> 48) & 0xf) != 0 || ((cap >> 37) & 1) == 0 || max_entries < 2)
    panic("nvme unsupported page size, command set or queue depth");
  nvme.db_stride = 4U << dstrd;
  if ((uint64)NVME_DB + 4ULL * nvme.db_stride > nvme.bar_size)
    panic("nvme doorbells outside BAR0");
  uint16 depth = max_entries < NVME_QDEPTH ? max_entries : NVME_QDEPTH;

  *reg32(NVME_CC) = 0;
  io_fence();
  wait_ready(0);
  memset(admin_sq, 0, sizeof(admin_sq));
  memset(admin_cq, 0, sizeof(admin_cq));
  memset(io_sq, 0, sizeof(io_sq));
  memset(io_cq, 0, sizeof(io_cq));
  nvme.admin.sq = admin_sq;
  nvme.admin.cq = admin_cq;
  nvme.admin.qid = 0;
  nvme.admin.depth = depth;
  nvme.admin.phase = 1;
  nvme.io.sq = io_sq;
  nvme.io.cq = io_cq;
  nvme.io.qid = 1;
  nvme.io.depth = depth;
  nvme.io.phase = 1;
  *reg32(NVME_INTMS) = 0xffffffff;
  *reg32(NVME_AQA) = ((uint32)(depth - 1) << 16) | (depth - 1);
  *reg64(NVME_ASQ) = (uint64)admin_sq;
  *reg64(NVME_ACQ) = (uint64)admin_cq;
  io_fence();
  *reg32(NVME_CC) = (6 << 16) | (4 << 20) | 1; // SQE=64, CQE=16, enable
  io_fence();
  wait_ready(1);

  identify(1, 0); // Identify Controller
  uint8 sqes = identify_data[512], cqes = identify_data[513];
  uint8 mdts = identify_data[77], vwc = identify_data[525];
  uint32 namespaces = *(uint32 *)(identify_data + 516);
  if ((sqes & 0xf) > 6 || (sqes >> 4) < 6 || (cqes & 0xf) > 4 ||
      (cqes >> 4) < 4 || namespaces < NVME_NSID)
    panic("nvme unsupported controller capabilities");

  identify(0, NVME_NSID); // Identify Namespace 1
  nvme.ns_blocks = *(uint64 *)identify_data;
  uint8 flbas = identify_data[26], format = flbas & 0xf;
  if (!nvme.ns_blocks || (flbas & 0x60) || format > identify_data[25])
    panic("nvme unsupported namespace format");
  uint32 offset = 128 + 4 * format;
  uint16 metadata =
    identify_data[offset] | ((uint16)identify_data[offset + 1] << 8);
  nvme.lba_shift = identify_data[offset + 2];
  if (metadata || (identify_data[29] & 7) ||
      (nvme.lba_shift != 9 && nvme.lba_shift != 10))
    panic("nvme requires 512/1024-byte LBA without metadata");

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_CREATE_CQ;
  cmd.prp1 = (uint64)io_cq;
  cmd.cdw10 = 1 | ((uint32)(depth - 1) << 16);
  cmd.cdw11 = 1; // physically contiguous, interrupt disabled
  submit(&nvme.admin, &cmd);
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_CREATE_SQ;
  cmd.prp1 = (uint64)io_sq;
  cmd.cdw10 = 1 | ((uint32)(depth - 1) << 16);
  cmd.cdw11 = 1 | (1 << 16); // physically contiguous, completion queue 1
  submit(&nvme.admin, &cmd);
  printk("NVME_READY nsid=%d lba=%d qdepth=%d vwc=%d mdts=%d blocks=%lu\n",
         NVME_NSID, 1 << nvme.lba_shift, depth, vwc & 1, mdts, nvme.ns_blocks);
}

void
nvme_disk_rw(struct buf *b, int write)
{
  struct nvme_command cmd;
  uint64 lba = (uint64)b->blockno * (BSIZE >> nvme.lba_shift);
  uint32 count = BSIZE >> nvme.lba_shift;
  if (lba >= nvme.ns_blocks || count > nvme.ns_blocks - lba)
    panic("nvme block outside namespace");
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = write ? NVME_IO_WRITE : NVME_IO_READ;
  cmd.nsid = NVME_NSID;
  cmd.prp1 = (uint64)b->data;
  // bcache is in identity-mapped, physically contiguous kernel BSS. A 1 KiB
  // transfer can cross at most one 4 KiB boundary, so PRP2 needs no list.
  if ((cmd.prp1 & (PGSIZE - 1)) + BSIZE > PGSIZE)
    cmd.prp2 = PGROUNDDOWN(cmd.prp1) + PGSIZE;
  cmd.cdw10 = (uint32)lba;
  cmd.cdw11 = (uint32)(lba >> 32);
  cmd.cdw12 = count - 1;
  acquire(&nvme.lock);
  submit(&nvme.io, &cmd);
  release(&nvme.lock);
}

void
nvme_disk_flush(void)
{
  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_IO_FLUSH;
  cmd.nsid = NVME_NSID;
  acquire(&nvme.lock);
  submit(&nvme.io, &cmd);
  release(&nvme.lock);
}
#endif // XV6_DISK_NVME
