// Test-only source-level PMFS persistence-operation trace.
#ifndef XV6_PMTRACE_H
#define XV6_PMTRACE_H

#include "types.h"

#ifdef PMFS_TRACE
void pmtrace_bind(void *base, uint64 nbytes);
int pmtrace_control(int mode);
void pmtrace_store(void *p, uint32 n, int atomic_word);
void pmtrace_flush(void *p);
void pmtrace_order(void);
void pmtrace_drain(void);
#else
static inline void
pmtrace_bind(void *base, uint64 nbytes)
{
  (void)base;
  (void)nbytes;
}
static inline int
pmtrace_control(int mode)
{
  (void)mode;
  return -1;
}
static inline void
pmtrace_store(void *p, uint32 n, int atomic_word)
{
  (void)p;
  (void)n;
  (void)atomic_word;
}
static inline void
pmtrace_flush(void *p)
{
  (void)p;
}
static inline void
pmtrace_order(void)
{
}
static inline void
pmtrace_drain(void)
{
}
#endif
#endif
