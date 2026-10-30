// Experimental no-KV/no-CDC direct-PMEM commit core.
// This is a test-only single-file slice, NOT the finished VFS filesystem or a
// stable on-media format. Never run it on a valuable NVDIMM image.
#ifndef XV6_DIRECTCORE_H
#define XV6_DIRECTCORE_H

enum {
  DIRECTCORE_MAX_SIZE = 512,
  DIRECTCORE_FORMAT = 1,
  DIRECTCORE_WRITE = 2,
  DIRECTCORE_READ = 3,
  DIRECTCORE_TRACE_WRITE = 4,
  DIRECTCORE_FORMAT_CONFIRM = 0x44434646,
};

#ifdef DIRECT_CORE_TEST
void directcore_init(void);
#endif

#endif
