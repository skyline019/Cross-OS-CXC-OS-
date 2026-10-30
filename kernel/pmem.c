// Persistent-memory window helpers + line-granular persist protocol.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmtrace.h"
#include "pmcontract.h"

// Word 0: MAP probe. Word 1: PMEM1 selftest. Word 2: durable across power-loss (PMEM2).
#define PMEM_MAGIC    0xA5A5FCFC55443322ULL
#define PMEM1_MAGIC   0xBEEFCAFE01020304ULL
#define PMEM2_DURABLE 0x504D454D32000001ULL // "PMEM2\0\0\1"

static int pmem_ok = 0;
static uint64 flush_lines_total = 0;
extern uint64 boot_fdt_pa;

#if defined(PMEM_NVDIMM) && !defined(PMEM_MAP)
#error "PMEM_NVDIMM requires PMEM_MAP"
#endif

#ifdef PMEM_NVDIMM
// Minimal, bounded reader for the generated QEMU research-machine FDT. It
// discovers only a top-level xv6,pmfs-nvdimm node and does not implement a
// general FDT library, ACPI/NFIT or NVDIMM namespace discovery.
static uint64 nvdimm_bytes;

static uint32
fdt_be32(const uint8 *p)
{
  return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) |
         p[3];
}

static int
fdt_string_eq(const uint8 *p, uint32 available, const char *value)
{
  uint32 i = 0;
  while (i < available && p[i] && value[i] && p[i] == (uint8)value[i])
    i++;
  return i < available && p[i] == 0 && value[i] == 0;
}

static uint64
pmem_nvdimm_from_fdt(uint64 pa)
{
  const uint8 *fdt;
  uint32 total, off_struct, off_strings, struct_len, strings_len, pos, end;
  uint32 depth = 0, found = 0, compatible = 0, have_reg = 0;
  uint64 reg_base = 0, reg_size = 0, result = 0;

  // This early boot probe runs before kalloc can overwrite the firmware DTB.
  // Bound every physical access to QEMU virt's ordinary RAM, not PMEM.
  if (pa < KERNBASE || pa > PHYSTOP - 40)
    return 0;
  fdt = (const uint8 *)pa;
  if (fdt_be32(fdt) != 0xd00dfeedu)
    return 0;
  total = fdt_be32(fdt + 4);
  off_struct = fdt_be32(fdt + 8);
  off_strings = fdt_be32(fdt + 12);
  strings_len = fdt_be32(fdt + 32);
  struct_len = fdt_be32(fdt + 36);
  if (total < 40 || total > 2 * 1024 * 1024 || total > PHYSTOP - pa ||
      off_struct > total || struct_len > total - off_struct ||
      off_strings > total || strings_len > total - off_strings)
    return 0;
  pos = off_struct;
  end = off_struct + struct_len;
  while (pos <= end && end - pos >= 4) {
    uint32 token = fdt_be32(fdt + pos);
    pos += 4;
    if (token == 1) { // FDT_BEGIN_NODE
      while (pos < end && fdt[pos])
        pos++;
      if (pos >= end || depth >= 64)
        return 0;
      pos++;
      if (pos > end - (pos % 4 ? 4 - pos % 4 : 0))
        return 0;
      pos = (pos + 3) & ~3u;
      depth++;
      if (depth == 2) {
        compatible = have_reg = 0;
        reg_base = reg_size = 0;
      }
    } else if (token == 2) { // FDT_END_NODE
      if (!depth)
        return 0;
      if (depth == 2 && compatible) {
        if (++found != 1 || have_reg != 1 || reg_base != PMEMBASE ||
            reg_size < 8 * PGSIZE || reg_size > PMEMSIZE ||
            (reg_size & (PGSIZE - 1)))
          return 0;
        result = reg_size;
      }
      depth--;
    } else if (token == 3) { // FDT_PROP
      uint32 len, nameoff, pad;
      if (end - pos < 8)
        return 0;
      len = fdt_be32(fdt + pos);
      nameoff = fdt_be32(fdt + pos + 4);
      pos += 8;
      if (nameoff >= strings_len || len > end - pos)
        return 0;
      pad = (4 - (len & 3)) & 3;
      if (pad > end - pos - len)
        return 0;
      if (depth == 2 &&
          fdt_string_eq(fdt + off_strings + nameoff, strings_len - nameoff,
                        "compatible") &&
          fdt_string_eq(fdt + pos, len, "xv6,pmfs-nvdimm"))
        compatible = 1;
      if (depth == 2 && fdt_string_eq(fdt + off_strings + nameoff,
                                      strings_len - nameoff, "reg")) {
        // Unrelated root nodes may have other reg widths. Defer the strict
        // 64-bit address/size check until a matching compatible is known.
        if (have_reg || len != 16) {
          have_reg = 2;
        } else {
          have_reg = 1;
          reg_base =
            ((uint64)fdt_be32(fdt + pos) << 32) | fdt_be32(fdt + pos + 4);
          reg_size =
            ((uint64)fdt_be32(fdt + pos + 8) << 32) | fdt_be32(fdt + pos + 12);
        }
      }
      pos += len + pad;
    } else if (token == 4) { // FDT_NOP
      continue;
    } else if (token == 9) { // FDT_END
      return depth == 0 && found == 1 ? result : 0;
    } else {
      return 0;
    }
  }
  return 0;
}
#endif

void
pmem_early_probe(void)
{
#ifdef PMEM_NVDIMM
  nvdimm_bytes = pmem_nvdimm_from_fdt(boot_fdt_pa);
#endif
}

void
pmem_init(void)
{
  pmem_ok = 0;
  flush_lines_total = 0;
#ifdef PMEM_ZICBOM
  printk("pmem: Zicbom CBO.CLEAN enabled, assumed block size=%d\n", PMEM_LINE);
#else
  printk("pmem: instrumentation only; no hardware cache writeback\n");
#endif

#ifdef PMEM_MAP
  // The ordinary-RAM path bounds the window by QEMU's DTB address; the
  // experimental device path instead requires its validated FDT node. Never
  // probe by writing an address that may contain user data.
  if (pmem_size() < 8 * PGSIZE) {
    printk("pmem: MAP unavailable: insufficient RAM or missing DTB\n");
    return;
  }
  pmem_ok = 1;
#ifdef PMEM_NVDIMM
  printk("pmem: MAP ready base=0x%lx usable=0x%lx fdt=0x%lx backend=nvdimm\n",
         (uint64)PMEMBASE, pmem_size(), boot_fdt_pa);
#else
  printk(
    "pmem: MAP ready base=0x%lx usable=0x%lx fdt=0x%lx backend=ordinary-ram\n",
    (uint64)PMEMBASE, pmem_size(), boot_fdt_pa);
#endif
#ifdef PMFS_SELFTEST
  {
    volatile uint64 *p = (volatile uint64 *)PMEMBASE;
    int survived = (p[2] == PMEM2_DURABLE);
    p[0] = PMEM_MAGIC;
    pmem_persist((void *)PMEMBASE, 8);
    if (p[0] != PMEM_MAGIC) {
      pmem_ok = 0;
      printk("pmem: MAP selftest failed\n");
    } else if (survived) {
      printk("ONBOX_PMFS_PMEM2_PASS persist=ok\n");
    }
  }
#endif
#else
  printk("pmem: stub (no PMEM_MAP) base=0x%lx — EMUL path\n", (uint64)PMEMBASE);
#endif
}

int
pmem_ready(void)
{
  return pmem_ok;
}

uint64
pmem_base_pa(void)
{
  return PMEMBASE;
}

uint64
pmem_size(void)
{
#ifdef PMEM_NVDIMM
  return nvdimm_bytes;
#else
  uint64 end = PMEMEND;
  // QEMU virt puts its DTB near the top of guest RAM and rewrites it at every
  // boot. Never allocate that page or anything above it as PMFS storage.
  // This only avoids the known overlap; it is not device discovery.
  if (boot_fdt_pa <= (uint64)PMEMBASE)
    return 0;
  if (boot_fdt_pa < end)
    end = boot_fdt_pa & ~(uint64)(PGSIZE - 1);
  return end - (uint64)PMEMBASE;
#endif
}

void *
pmem_base_va(void)
{
  return (void *)PMEMBASE;
}

const char *
pmem_backend_name(void)
{
#ifdef PMEM_NVDIMM
  return pmem_ok ? "nvdimm" : "unavailable";
#elif defined(PMEM_MAP)
  return pmem_ok ? "map" : "unavailable";
#else
  return "emul";
#endif
}

uint64
pmem_flush_lines(void)
{
  return flush_lines_total;
}

void
pmem_flush_lines_reset(void)
{
  flush_lines_total = 0;
}

// The default is instrumentation only, not a cache writeback. The opt-in
// Zicbom path issues CBO.CLEAN with an assumed 64-byte block size; it requires
// a compatible platform, privilege setup and a documented persistence domain.
// QEMU TCG implements CBO as a no-op and cannot validate hardware durability.
void
pmem_flush_batch_begin(void)
{
  // Order prior CPU writes before the subsequent cache-block operations.
  // This is not a persistence completion/drain.
  asm volatile("fence rw, rw" ::: "memory");
  pmtrace_order();
}

void
pmem_flush_batch(void *p, uint64 n)
{
  uint64 a, end;
#ifndef PMEM_ZICBOM
  volatile uint8 *v;
#endif

  if (p == 0 || n == 0)
    return;

  if (n > ~(uint64)0 - (uint64)p)
    panic("pmem flush range overflow");
  a = (uint64)p & ~(uint64)(PMEM_LINE - 1);
  end = (uint64)p + n;
  for (;;) {
#ifdef PMEM_ZICBOM
    // CBO.CLEAN encoding: opcode=0x0f, funct3=2, imm=1, rd=x0.
    // .insn avoids requiring a recent assembler's zicbom mnemonic parser.
    asm volatile(".insn i 0x0f, 2, x0, %0, 1" : : "r"(a) : "memory");
#else
    v = (volatile uint8 *)a;
    (void)*v; // instrumentation-only stand-in, NOT a persistence operation
#endif
    flush_lines_total++;
    pmtrace_flush((void *)a);
    if (end - a <= PMEM_LINE)
      break;
    a += PMEM_LINE;
  }
}

void
pmem_flush(void *p, uint64 n)
{
  if (p == 0 || n == 0)
    return;
  pmem_flush_batch(p, n);
  // Preserve the historical ordering semantics for existing PMFS callers.
  __sync_synchronize();
}

void
pmem_fence(void)
{
  __sync_synchronize();
  asm volatile("fence rw, rw" ::: "memory");
  pmtrace_drain();
}

void
pmem_persist(void *p, uint64 n)
{
  pmem_flush(p, n);
  pmem_fence();
}

int
pmem_selftest(void)
{
  volatile uint64 *p;
  uint64 got;

  if (!pmem_ok)
    return -1;

  p = (volatile uint64 *)PMEMBASE;
  p[1] = PMEM1_MAGIC;
  pmem_persist((void *)(PMEMBASE + 8), 8);
  got = p[1];
  if (got != PMEM1_MAGIC)
    return -2;
  return 0;
}

void
pmem_contract_map(void)
{
  volatile uint64 *p;
  int rc;
  if (!pmem_ok)
    return;
  rc = pmem_selftest();
  if (rc != 0) {
    pmcontract_fail("PMEM1", rc < 0 ? -rc : rc);
    return;
  }
  pmcontract_pass("PMEM1", "magic=ok");

  // Plant durable marker for next cold boot (PMEM2 / memory-backend-file).
  p = (volatile uint64 *)PMEMBASE;
  p[2] = PMEM2_DURABLE;
  pmem_persist((void *)(PMEMBASE + 16), 8);
}

// Ordered persist protocol self-test (合题：缓存行刷写).
int
pmem_persist_selftest(void)
{
  static uint8 buf[PMEM_LINE * 3];
  uint64 before, after, expect;
  int i;

  pmem_flush_lines_reset();
  before = pmem_flush_lines();
  for (i = 0; i < (int)sizeof(buf); i++)
    buf[i] = (uint8)(i * 17);

  // store data → flush data → fence
  pmem_flush(buf, sizeof(buf));
  pmem_fence();
  after = pmem_flush_lines();
  expect = (sizeof(buf) + PMEM_LINE - 1) / PMEM_LINE;
  if (after - before < expect) {
    pmcontract_fail("PERSIST", 1);
    return -1;
  }
  // Second range spanning two lines
  before = pmem_flush_lines();
  pmem_persist(buf + PMEM_LINE / 2, PMEM_LINE);
  after = pmem_flush_lines();
  if (after <= before) {
    pmcontract_fail("PERSIST", 2);
    return -2;
  }

  printk(
    "ONBOX_PMFS_PERSIST_PASS flush_lines=%d line=%d proto=store_flush_fence\n",
    (int)pmem_flush_lines(), PMEM_LINE);
  pmcontract_mark("PERSIST", 1);
  return 0;
}
