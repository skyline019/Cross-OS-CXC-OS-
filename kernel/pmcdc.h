// Localized CDC (Rabin-style rolling hash) for xv6-PMEM page device.
#ifndef XV6_PMCDC_H
#define XV6_PMCDC_H

#include "types.h"

enum {
  PMCDC_MODE_CDC = 0,
  PMCDC_MODE_FIXED = 1, // ablation only
  PMCDC_WIN = 48,       // rolling window bytes
  PMCDC_MIN_DEF = 64,
  PMCDC_AVG_DEF = 128,
  PMCDC_MAX_DEF = 512, // == PM_VAL_MAX
};

struct pmcdc {
  int mode; // CDC or FIXED
  uint32 min_sz, avg_sz, max_sz;
  uint32 mask; // avg-normalized cut mask
  // Rolling state (persists across stream_write calls).
  uint64 hash;
  uint8 win[PMCDC_WIN];
  uint32 wlen;  // bytes filled in win
  uint32 wpos;  // next write index in win
  uint32 since; // bytes since last cut
  // Stats
  uint64 ncuts;
  uint64 nbytes_in;
  uint64 sum_chunk;
};

void pmcdc_init(struct pmcdc *c, int mode, uint32 min_sz, uint32 avg_sz,
                uint32 max_sz);
void pmcdc_reset(struct pmcdc *c);

// Feed one byte; returns 1 if a cut should happen *after* this byte (chunk ends).
int pmcdc_feed(struct pmcdc *c, uint8 b);

// Force cut at max / end-of-stream.
int pmcdc_should_force(struct pmcdc *c);

uint64 pmcdc_content_fp(const void *buf, uint32 len); // FNV-ish fingerprint

int pmcdc_selftest(void);

#endif
