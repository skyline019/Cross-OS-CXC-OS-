#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmtrace.h"

#ifdef PMFS_TRACE
// This instrumentation is for one controlled workload on one hart only. It
// records the bytes visible after each PMFS device write, split into 64-byte
// fragments, not individual machine instructions. It changes timing and is
// deliberately absent from ordinary kernels.
static uint8 *trace_base;
static uint64 trace_bytes;
static uint32 trace_sequence;
static int trace_active;
static const char hex[] = "0123456789abcdef";

void
pmtrace_bind(void *base, uint64 nbytes)
{
  trace_base = base;
  trace_bytes = nbytes;
  trace_active = 0;
}

int
pmtrace_control(int mode)
{
  if (mode == 1 && trace_base && !trace_active) {
    trace_sequence = 0;
    trace_active = 1;
    printk("PMTRACE BEGIN version=1 line=64 bytes=%lu\n", trace_bytes);
    return 0;
  }
  if (mode == 2 && trace_active) {
    printk("PMTRACE ACK seq=%u\n", trace_sequence++);
    return 0;
  }
  if (mode == 0 && trace_active) {
    printk("PMTRACE END seq=%u\n", trace_sequence++);
    trace_active = 0;
    return 0;
  }
  return -1;
}

void
pmtrace_store(void *p, uint32 n, int atomic_word)
{
  uint64 addr = (uint64)p, offset, i;
  uint32 count, j;
  const uint8 *bytes = p;
  char encoded[129];
  if (!trace_active || addr < (uint64)trace_base)
    return;
  offset = addr - (uint64)trace_base;
  if (offset >= trace_bytes || n > trace_bytes - offset)
    panic("pmtrace store bounds");
  if (atomic_word && (n != 8 || (offset & 7)))
    panic("pmtrace word alignment");
  for (i = 0; i < n; i += count) {
    count = n - i;
    // Keep each range within a single assumed cache line.
    if (count > 64 - ((offset + i) & 63))
      count = 64 - ((offset + i) & 63);
    for (j = 0; j < count; j++) {
      encoded[2 * j] = hex[bytes[i + j] >> 4];
      encoded[2 * j + 1] = hex[bytes[i + j] & 15];
    }
    encoded[2 * count] = 0;
    printk("PMTRACE W seq=%u off=%lu n=%u atomic=%d data=%s\n",
           trace_sequence++, offset + i, count, atomic_word, encoded);
  }
}

void
pmtrace_flush(void *p)
{
  uint64 addr = (uint64)p, offset;
  if (!trace_active || addr < (uint64)trace_base)
    return;
  offset = addr - (uint64)trace_base;
  if (offset >= trace_bytes)
    return;
  if ((offset & 63) || trace_bytes - offset < 64)
    panic("pmtrace flush bounds");
  printk("PMTRACE F seq=%u off=%lu\n", trace_sequence++, offset);
}

void
pmtrace_order(void)
{
  if (trace_active)
    printk("PMTRACE O seq=%u\n", trace_sequence++);
}

void
pmtrace_drain(void)
{
  if (trace_active)
    printk("PMTRACE D seq=%u\n", trace_sequence++);
}
#endif
