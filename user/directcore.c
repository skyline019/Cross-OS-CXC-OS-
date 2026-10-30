// Disposable NVDIMM test client for the one-file direct publication core.
// This is not the finished PMEM filesystem or its VFS interface.
#include "kernel/types.h"
#include "kernel/directcore.h"
#include "user/user.h"

static uint8 data[DIRECTCORE_MAX_SIZE], actual[DIRECTCORE_MAX_SIZE];

static void
pattern(int generation, int size)
{
  for (int i = 0; i < size; i++)
    data[i] = (uint8)(i * 29 + (i >> 3) * 7 + generation * 31);
}

static void __attribute__((noreturn))
fail(char *reason)
{
  printf("DIRECTCORE_TEST_FAIL reason=%s\n", reason);
  exit(1);
}

int
main(int argc, char **argv)
{
  int generation, size, stage = 0, n, op;
  if (argc == 3 && strcmp(argv[1], "format") == 0 &&
      strcmp(argv[2], "--force") == 0) {
    if (directcore(DIRECTCORE_FORMAT, (void *)DIRECTCORE_FORMAT_CONFIRM, 0, 0))
      fail("format");
    printf("DIRECTCORE_FORMAT_PASS\n");
    exit(0);
  }
  if (argc == 2 && strcmp(argv[1], "isolation") == 0) {
    if (pmfs_format() != -1 || pmfs_trace(1) != -1 ||
        pmfs_put(1, data, 1) != -1 || pmfs_fopen("test") != -1 ||
        pmfs_compact() != -1)
      fail("legacy-isolation");
    printf("DIRECTCORE_ISOLATION_PASS legacy_calls=5\n");
    exit(0);
  }
  if (argc == 2 && strcmp(argv[1], "probe") == 0) {
    n = directcore(DIRECTCORE_READ, actual, sizeof(actual), 0);
    if (n < 0)
      printf("DIRECTCORE_PROBE_REJECTED\n");
    else
      printf("DIRECTCORE_PROBE_VALID size=%d\n", n);
    exit(0);
  }
  if (argc == 5 && strcmp(argv[1], "which") == 0) {
    int older = atoi(argv[2]), newer = atoi(argv[3]);
    size = atoi(argv[4]);
    if (older < 0 || newer < 0 || size < 0 || size > DIRECTCORE_MAX_SIZE)
      fail("range");
    n = directcore(DIRECTCORE_READ, actual, sizeof(actual), 0);
    if (n != size)
      fail("content");
    pattern(older, size);
    if (memcmp(actual, data, size) == 0) {
      printf("DIRECTCORE_VERSION old\n");
      exit(0);
    }
    pattern(newer, size);
    if (memcmp(actual, data, size) == 0) {
      printf("DIRECTCORE_VERSION new\n");
      exit(0);
    }
    fail("content");
  }
  if (argc != 4 && argc != 5)
    fail("args");
  if (argc == 5) {
    if (strcmp(argv[1], "fault") != 0)
      fail("args");
    stage = atoi(argv[2]);
    generation = atoi(argv[3]);
    size = atoi(argv[4]);
    if (stage < 1 || stage > 6)
      fail("stage");
  } else {
    generation = atoi(argv[2]);
    size = atoi(argv[3]);
  }
  if (generation < 0 || size < 0 || size > DIRECTCORE_MAX_SIZE)
    fail("range");
  if (strcmp(argv[1], "concurrent") == 0) {
    int loops = generation, status, pids[2];
    if (loops < 1 || loops > 1000 || size < 1)
      fail("range");
    for (int child = 0; child < 2; child++) {
      pids[child] = fork();
      if (pids[child] < 0)
        fail("fork");
      if (pids[child] == 0) {
        for (int i = 0; i < loops; i++) {
          pattern(child + 1, size);
          if (directcore(DIRECTCORE_WRITE, data, size, 0) != 0 ||
              directcore(DIRECTCORE_READ, actual, sizeof(actual), 0) != size)
            fail("concurrent-io");
          pattern(1, size);
          if (memcmp(actual, data, size) != 0) {
            pattern(2, size);
            if (memcmp(actual, data, size) != 0)
              fail("concurrent-torn-view");
          }
        }
        exit(0);
      }
    }
    for (int i = 0; i < 2; i++)
      if (wait(&status) < 0 || status != 0)
        fail("concurrent-child");
    n = directcore(DIRECTCORE_READ, actual, sizeof(actual), 0);
    if (n != size)
      fail("concurrent-final");
    pattern(1, size);
    if (memcmp(actual, data, size) != 0) {
      pattern(2, size);
      if (memcmp(actual, data, size) != 0)
        fail("concurrent-final");
    }
    printf("DIRECTCORE_CONCURRENT_PASS writers=2 loops=%d size=%d\n", loops,
           size);
    exit(0);
  }
  if (strcmp(argv[1], "soak") == 0) {
    int cycles = generation;
    if (cycles < 1 || cycles > 10000)
      fail("range");
    for (int i = 1; i <= cycles; i++) {
      pattern(i, size);
      if (directcore(DIRECTCORE_WRITE, data, size, 0) != 0 ||
          directcore(DIRECTCORE_READ, actual, sizeof(actual), 0) != size ||
          memcmp(actual, data, size) != 0)
        fail("soak");
    }
    printf("DIRECTCORE_SOAK_PASS cycles=%d size=%d\n", cycles, size);
    exit(0);
  }
  pattern(generation, size);
  if (strcmp(argv[1], "check") == 0) {
    n = directcore(DIRECTCORE_READ, actual, sizeof(actual), 0);
    if (n != size || memcmp(actual, data, size) != 0)
      fail("content");
    printf("DIRECTCORE_CHECK_PASS generation=%d size=%d\n", generation, size);
    exit(0);
  }
  if (strcmp(argv[1], "write") == 0 || strcmp(argv[1], "fault") == 0 ||
      strcmp(argv[1], "trace") == 0) {
    op =
      strcmp(argv[1], "trace") == 0 ? DIRECTCORE_TRACE_WRITE : DIRECTCORE_WRITE;
    if (directcore(op, data, size, stage) != 0)
      fail("write");
    if (stage)
      fail("fault-not-hit");
    printf("DIRECTCORE_WRITE_PASS generation=%d size=%d\n", generation, size);
    exit(0);
  }
  fail("args");
}
