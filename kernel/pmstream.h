// Localized streaming window over CDC + pmalgo seal.
#ifndef XV6_PMSTREAM_H
#define XV6_PMSTREAM_H

#include "types.h"
#include "pmcdc.h"
#include "pmalgo.h"

enum {
  PMSTREAM_WIN = 8192, // ingest window bound (背压)
  PMSTREAM_MAX_CHUNKS_PENDING = 16,
  PMSTREAM_SLOTS = 8,
};

struct pmstream_chunk {
  uint64 key;
  uint32 len;
};

struct pmstream {
  int used;
  int ino;
  int auto_seal;  // 1 = seal each chunk (when !batch_seal)
  int batch_seal; // 1 = seal on flush only
  struct pmcdc cdc;
  uint8 win[PMSTREAM_WIN];
  uint32 wlen;
  uint32 file_off; // logical bytes accepted
  uint32 nchunk;   // chunks emitted for this stream
  uint64 last_key;
  uint64 pending_puts;
  struct pmstream_chunk chunks[PMSTREAM_MAX_CHUNKS_PENDING];
};

void pmstream_init_global(void);
struct pmstream *pmstream_alloc(int ino, int mode_cdc, int batch_seal);
void pmstream_free(struct pmstream *s);
int pmstream_any_active(void);

// Write bytes into window; may emit chunks via pm_put; optional mid-seal.
int pmstream_write(struct pmstream *s, const void *src, uint32 len);
// Flush remaining window → put + pm_seal (durable).
int pmstream_flush(struct pmstream *s);
int pmstream_selftest(void);

#endif
