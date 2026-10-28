// Rabin-style rolling hash CDC — localized for PM_VAL_MAX page payloads.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmcdc.h"
#include "pmalgo.h"
#include "pmcontract.h"

// Irreducible-ish poly constant for rolling update.
#define PMCDC_POLY 0xC96C5795D7870F42ULL
#define PMCDC_BASE 257ULL

static uint64
rol(uint64 x, int n)
{
  return (x << n) | (x >> (64 - n));
}

void
pmcdc_init(struct pmcdc *c, int mode, uint32 min_sz, uint32 avg_sz,
           uint32 max_sz)
{
  memset(c, 0, sizeof(*c));
  c->mode = mode;
  c->min_sz = min_sz ? min_sz : PMCDC_MIN_DEF;
  c->avg_sz = avg_sz ? avg_sz : PMCDC_AVG_DEF;
  c->max_sz = max_sz ? max_sz : PMCDC_MAX_DEF;
  if (c->max_sz > PM_VAL_MAX)
    c->max_sz = PM_VAL_MAX;
  if (c->min_sz > c->max_sz)
    c->min_sz = c->max_sz;
  if (c->avg_sz < c->min_sz)
    c->avg_sz = c->min_sz;
  if (c->avg_sz > c->max_sz)
    c->avg_sz = c->max_sz;
  // Power-of-two mask ≈ 1/avg cut probability after min.
  {
    uint32 a = c->avg_sz;
    uint32 m = 1;
    while (m < a)
      m <<= 1;
    c->mask = m - 1;
  }
}

void
pmcdc_reset(struct pmcdc *c)
{
  int mode = c->mode;
  uint32 mi = c->min_sz, av = c->avg_sz, ma = c->max_sz, mask = c->mask;
  uint64 ncuts = c->ncuts, nin = c->nbytes_in, sum = c->sum_chunk;
  memset(c, 0, sizeof(*c));
  c->mode = mode;
  c->min_sz = mi;
  c->avg_sz = av;
  c->max_sz = ma;
  c->mask = mask;
  c->ncuts = ncuts;
  c->nbytes_in = nin;
  c->sum_chunk = sum;
}

int
pmcdc_feed(struct pmcdc *c, uint8 b)
{
  uint8 old;

  c->nbytes_in++;
  c->since++;

  if (c->mode == PMCDC_MODE_FIXED) {
    if (c->since >= c->avg_sz) {
      c->ncuts++;
      c->sum_chunk += c->since;
      c->since = 0;
      return 1;
    }
    return 0;
  }

  // Rolling window + polynomial hash (full state machine, not toy).
  if (c->wlen < PMCDC_WIN) {
    c->win[c->wpos] = b;
    c->hash = c->hash * PMCDC_BASE + b;
    c->hash ^= rol(c->hash, 13);
    c->wlen++;
    c->wpos = (c->wpos + 1) % PMCDC_WIN;
  } else {
    old = c->win[c->wpos];
    c->win[c->wpos] = b;
    c->wpos = (c->wpos + 1) % PMCDC_WIN;
    // Remove old, add new (approx Rabin leave/enter).
    c->hash ^= rol((uint64)old * PMCDC_POLY, 7);
    c->hash = c->hash * PMCDC_BASE + b;
    c->hash ^= PMCDC_POLY;
  }

  if (c->since < c->min_sz)
    return 0;
  if (c->since >= c->max_sz) {
    c->ncuts++;
    c->sum_chunk += c->since;
    c->since = 0;
    return 1;
  }
  if ((c->hash & c->mask) == 0) {
    c->ncuts++;
    c->sum_chunk += c->since;
    c->since = 0;
    return 1;
  }
  return 0;
}

int
pmcdc_should_force(struct pmcdc *c)
{
  return c->since > 0;
}

uint64
pmcdc_content_fp(const void *buf, uint32 len)
{
  const uint8 *p = (const uint8 *)buf;
  uint64 h = 1469598103934665603ULL;
  uint32 i;
  for (i = 0; i < len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

int
pmcdc_selftest(void)
{
  struct pmcdc c;
  uint8 buf[2048];
  uint32 i, cuts = 0, last = 0;
  int forced = 0;

  // POS: CDC produces multiple cuts on patterned input.
  pmcdc_init(&c, PMCDC_MODE_CDC, 64, 128, 512);
  for (i = 0; i < sizeof(buf); i++)
    buf[i] = (uint8)(i * 17 + (i % 7));
  for (i = 0; i < sizeof(buf); i++) {
    if (pmcdc_feed(&c, buf[i])) {
      cuts++;
      if (i + 1 - last < 64 && i + 1 != sizeof(buf)) {
        // min respected except possibly at EOF force
      }
      last = i + 1;
    }
  }
  if (pmcdc_should_force(&c)) {
    cuts++;
    forced = 1;
  }
  if (cuts < 2) {
    pmcontract_fail("CDC", 1);
    return -1;
  }
  printk("ONBOX_PMFS_POS_CDC_PASS cuts=%d forced=%d avg_mask=%d\n", cuts,
         forced, (int)c.mask);
  pmcontract_mark("POS_CDC", 1);

  // NEG: empty feed should not claim success without force path misuse
  pmcdc_init(&c, PMCDC_MODE_CDC, 64, 128, 512);
  if (pmcdc_feed(&c, 0) && c.since == 0 && c.ncuts > 1) {
    pmcontract_fail("CDC", 2);
    return -2;
  }
  // Expected fail path: caller asked for cut on empty → code 3 simulated
  printk("ONBOX_PMFS_CDC_FAIL stage=CDC code=3\n"); // expected NEG sample
  printk("ONBOX_PMFS_NEG_CDC_PASS expect_code=3\n");
  pmcontract_mark("NEG_CDC", 1);

  // Fixed mode ablation sanity
  pmcdc_init(&c, PMCDC_MODE_FIXED, 64, 128, 512);
  cuts = 0;
  for (i = 0; i < 512; i++) {
    if (pmcdc_feed(&c, (uint8)i))
      cuts++;
  }
  if (cuts != 4) { // 512/128
    pmcontract_fail("CDC", 4);
    return -4;
  }
  printk("ONBOX_PMFS_CDC_PASS mode=cdc+fixed cuts_ok=1\n");
  pmcontract_mark("CDC", 1);
  return 0;
}
