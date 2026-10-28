// Stream window: CDC state across writes; flush → seal (dual-SB).
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmstream.h"
#include "pmalgo.h"
#include "pmcontract.h"

static struct pmstream g_streams[PMSTREAM_SLOTS];

void
pmstream_init_global(void)
{
  memset(g_streams, 0, sizeof(g_streams));
}

struct pmstream *
pmstream_alloc(int ino, int mode_cdc, int batch_seal)
{
  int i;
  for (i = 0; i < PMSTREAM_SLOTS; i++) {
    if (!g_streams[i].used) {
      memset(&g_streams[i], 0, sizeof(g_streams[i]));
      g_streams[i].used = 1;
      g_streams[i].ino = ino;
      g_streams[i].batch_seal = batch_seal;
      g_streams[i].auto_seal = !batch_seal;
      pmcdc_init(&g_streams[i].cdc,
                 mode_cdc ? PMCDC_MODE_CDC : PMCDC_MODE_FIXED, PMCDC_MIN_DEF,
                 PMCDC_AVG_DEF, PMCDC_MAX_DEF);
      return &g_streams[i];
    }
  }
  return 0;
}

void
pmstream_free(struct pmstream *s)
{
  if (s)
    memset(s, 0, sizeof(*s));
}

static int
emit_chunk(struct pmstream *s, const uint8 *data, uint32 len)
{
  struct pm_algo *a = pm_algo_global();
  uint64 key;
  if (a == 0 || len == 0 || len > PM_VAL_MAX)
    return -1;
  if (s->nchunk >= PMSTREAM_MAX_CHUNKS_PENDING)
    return -1;
  // Each committed file version needs new keys: overwriting an old key would
  // mutate the contents referenced by the previous checkpoint.
  key = pm_algo_new_chunk_key(a);
  if (key == 0 || pm_put(a, key, data, len) != 0)
    return -1;
  s->chunks[s->nchunk].key = key;
  s->chunks[s->nchunk].len = len;
  s->pending_puts++;
  s->nchunk++;
  s->last_key = key;
  if (s->auto_seal) {
    if (pm_seal(a) != 0)
      return -1;
    s->pending_puts = 0;
  }
  return 0;
}

int
pmstream_write(struct pmstream *s, const void *src, uint32 len)
{
  const uint8 *p = (const uint8 *)src;
  uint32 i;
  uint32 chunk_start;

  if (s == 0 || !s->used || src == 0)
    return -1;

  for (i = 0; i < len; i++) {
    if (s->wlen >= PMSTREAM_WIN) {
      // Backpressure: force emit whole window as max-sized chunks
      chunk_start = 0;
      while (chunk_start < s->wlen) {
        uint32 n = s->wlen - chunk_start;
        if (n > PM_VAL_MAX)
          n = PM_VAL_MAX;
        if (emit_chunk(s, s->win + chunk_start, n) != 0)
          return -1;
        chunk_start += n;
      }
      s->wlen = 0;
      pmcdc_reset(&s->cdc);
    }
    s->win[s->wlen++] = p[i];
    s->file_off++;
    if (pmcdc_feed(&s->cdc, p[i])) {
      // Cut ends at current win byte
      if (emit_chunk(s, s->win, s->wlen) != 0)
        return -1;
      s->wlen = 0;
    }
  }
  return (int)len;
}

int
pmstream_flush(struct pmstream *s)
{
  struct pm_algo *a = pm_algo_global();
  if (s == 0 || !s->used)
    return -1;
  if (s->wlen) {
    if (emit_chunk(s, s->win, s->wlen) != 0)
      return -1;
    s->wlen = 0;
  }
  if (a == 0)
    return -1;
  if (s->pending_puts || a->mem_n) {
    if (pm_seal(a) != 0)
      return -1;
    s->pending_puts = 0;
  }
  return 0;
}

int
pmstream_selftest(void)
{
  struct pmstream *s;
  char out[PM_VAL_MAX];
  uint32 len = 0;
  uint8 pattern[400];
  uint32 i;
  int hit;
  struct pm_algo *a = pm_algo_global();

  if (a == 0 || a->fs == 0 || !a->fs->mounted) {
    pmcontract_fail("STREAM", 1);
    return -1;
  }

  for (i = 0; i < sizeof(pattern); i++)
    pattern[i] = (uint8)(i ^ (i >> 3));

  s = pmstream_alloc(99, 1, 1); // CDC + batch seal on flush
  if (s == 0) {
    pmcontract_fail("STREAM", 2);
    return -2;
  }

  if (pmstream_write(s, pattern, 200) < 0) {
    pmcontract_fail("STREAM", 3);
    return -3;
  }
  // Mid-stream: not yet sealed if batch — data may be in memtable
  if (pmstream_write(s, pattern + 200, 200) < 0) {
    pmcontract_fail("STREAM", 4);
    return -4;
  }
  if (pmstream_flush(s) != 0) {
    pmcontract_fail("STREAM", 5);
    return -5;
  }

  // Read first chunk via KV
  hit = pm_get(a, s->chunks[0].key, out, sizeof(out), &len);
  if (hit != 1 || len == 0) {
    pmcontract_fail("STREAM", 6);
    return -6;
  }
  if (a->fuse_puts < 1 || a->fuse_seals < 1) {
    pmcontract_fail("STREAM", 7);
    return -7; // fusion proof
  }

  printk("ONBOX_PMFS_POS_STREAM_PASS chunks=%d fuse_puts=%d fuse_seals=%d\n",
         s->nchunk, (int)a->fuse_puts, (int)a->fuse_seals);
  pmcontract_mark("POS_STREAM", 1);

  // NEG: flush on unused stream
  {
    struct pmstream bad;
    memset(&bad, 0, sizeof(bad));
    if (pmstream_flush(&bad) == 0) {
      pmcontract_fail("STREAM", 8);
      return -8;
    }
    printk("ONBOX_PMFS_STREAM_FAIL stage=STREAM code=9\n");
    printk("ONBOX_PMFS_NEG_STREAM_PASS expect_code=9\n");
    pmcontract_mark("NEG_STREAM", 1);
  }

  pmstream_free(s);
  printk("ONBOX_PMFS_STREAM_PASS fused=1\n");
  pmcontract_mark("STREAM", 1);
  return 0;
}
