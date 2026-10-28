// Matched file transactions for the QEMU software-path experiment.
// This is not a physical PMEM benchmark; the two fsync implementations have
// different durability contracts. Only run on disposable filesystem images.
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/pmstats.h"
#include "user/user.h"

enum { MAX_SIZE = 1024, MAX_SAMPLES = 56, WARMUP = 4 };
static char payload[MAX_SIZE], result[MAX_SIZE];

static void
pattern(int size, int generation)
{
  int i;
  for (i = 0; i < size; i++)
    payload[i] = (char)(i * 17 + generation * 31 + (i >> 3));
}

static int
transaction(const char *path, int size, int fresh)
{
  int fd;
  if (fresh) {
    if ((fd = open(path, O_RDONLY)) >= 0) {
      close(fd);
      return -1;
    }
    fd = open(path, O_CREATE | O_RDWR);
  } else {
    fd = open(path, O_RDWR);
  }
  if (fd < 0 || lseek(fd, 0, 0) != 0 || write(fd, payload, size) != size ||
      fsync(fd) != 0 || close(fd) != 0 || (fd = open(path, O_RDONLY)) < 0 ||
      read(fd, result, size) != size || memcmp(result, payload, size) != 0 ||
      read(fd, result, 1) != 0 || close(fd) != 0 ||
      (fresh && unlink(path) != 0))
    return -1;
  return 0;
}

static int
make_initial(const char *path, int size)
{
  int fd = open(path, O_CREATE | O_RDWR);
  if (fd < 0 || write(fd, payload, size) != size || fsync(fd) != 0 ||
      close(fd) != 0)
    return -1;
  return 0;
}

static int
measure(const char *path, int size, int fresh, unsigned long *ticks)
{
  unsigned long start = cycles(), end;
  int rc = transaction(path, size, fresh);
  end = cycles();
  if (rc != 0 || end <= start)
    return -1;
  *ticks = end - start;
  return 0;
}

static int
batch(const char *path, int size, int fresh, int count, unsigned long *ticks)
{
  unsigned long start = cycles(), end;
  int i;
  for (i = 0; i < count; i++) {
    pattern(size, i + 1000);
    if (transaction(path, size, fresh) != 0)
      return -1;
  }
  end = cycles();
  if (end <= start)
    return -1;
  *ticks = end - start;
  return 0;
}

int
main(int argc, char **argv)
{
  const char *mode;
  int samples, size, fresh, i, batch_order;
  unsigned long pm, xv;
  struct pmfs_metrics before, after;
  const char *pm_path = "/pm/p/f", *xv_path = "/b/f";

  if (argc != 5) {
    printf("PMFS_PERF_FAIL stage=args\n");
    exit(1);
  }
  mode = argv[1];
  samples = atoi(argv[2]);
  size = atoi(argv[3]);
  batch_order = atoi(argv[4]);
  fresh = strcmp(mode, "roundtrip") == 0;
  if ((!fresh && strcmp(mode, "overwrite") != 0) || samples < 1 ||
      samples > MAX_SAMPLES || (batch_order != 0 && batch_order != 1) ||
      (size != 8 && size != 64 && size != 512 && size != 1024)) {
    printf("PMFS_PERF_FAIL stage=args\n");
    exit(1);
  }
  // Fresh disposable boots are expected. Refuse to overwrite named files.
  mkdir("/pm/p");
  mkdir("/b");
  if ((i = open(pm_path, O_RDONLY)) >= 0) {
    close(i);
    printf("PMFS_PERF_FAIL stage=existing-pm\n");
    exit(1);
  }
  if ((i = open(xv_path, O_RDONLY)) >= 0) {
    close(i);
    printf("PMFS_PERF_FAIL stage=existing-xv\n");
    exit(1);
  }
  if (!fresh) {
    pattern(size, -1);
    if (make_initial(pm_path, size) != 0 || make_initial(xv_path, size) != 0) {
      printf("PMFS_PERF_FAIL stage=setup\n");
      exit(1);
    }
  }
  for (i = -WARMUP; i < samples; i++) {
    pattern(size, i);
    // Balance order within each boot. Metrics are measured outside the timed
    // interval and include the complete PMFS transaction, not hardware I/O.
    if (i % 2 == 0) {
      if (pmfs_metrics(&before, sizeof(before)) != 0 ||
          measure(pm_path, size, fresh, &pm) != 0 ||
          pmfs_metrics(&after, sizeof(after)) != 0 ||
          measure(xv_path, size, fresh, &xv) != 0) {
        printf("PMFS_PERF_FAIL stage=transaction i=%d\n", i);
        exit(1);
      }
    } else {
      if (measure(xv_path, size, fresh, &xv) != 0 ||
          pmfs_metrics(&before, sizeof(before)) != 0 ||
          measure(pm_path, size, fresh, &pm) != 0 ||
          pmfs_metrics(&after, sizeof(after)) != 0) {
        printf("PMFS_PERF_FAIL stage=transaction i=%d\n", i);
        exit(1);
      }
    }
    if (i >= 0)
      printf("PMFS_PERF_SAMPLE mode=%s size=%d i=%d pm_ticks=%lu xv_ticks=%lu "
             "pm_write=%lu pm_read=%lu pm_flush=%lu order=%d\n",
             mode, size, i, pm, xv,
             after.dev_write_bytes - before.dev_write_bytes,
             after.dev_read_bytes - before.dev_read_bytes,
             after.requested_flush_lines - before.requested_flush_lines, i % 2);
  }
  // Separately time complete uninstrumented batches, including payload
  // generation and loop overhead but excluding printing and metrics syscalls.
  // Alternate backend order across boots; individual transaction latency
  // samples above remain interleaved and separately timed.
  if (batch_order == 0) {
    if (pmfs_metrics(&before, sizeof(before)) != 0 ||
        batch(pm_path, size, fresh, samples, &pm) != 0 ||
        pmfs_metrics(&after, sizeof(after)) != 0 ||
        batch(xv_path, size, fresh, samples, &xv) != 0) {
      printf("PMFS_PERF_FAIL stage=batch\n");
      exit(1);
    }
  } else {
    if (batch(xv_path, size, fresh, samples, &xv) != 0 ||
        pmfs_metrics(&before, sizeof(before)) != 0 ||
        batch(pm_path, size, fresh, samples, &pm) != 0 ||
        pmfs_metrics(&after, sizeof(after)) != 0) {
      printf("PMFS_PERF_FAIL stage=batch\n");
      exit(1);
    }
  }
  printf(
    "PMFS_PERF_BATCH mode=%s size=%d count=%d pm_ticks=%lu xv_ticks=%lu "
    "pm_write=%lu pm_read=%lu pm_flush=%lu order=%d\n",
    mode, size, samples, pm, xv, after.dev_write_bytes - before.dev_write_bytes,
    after.dev_read_bytes - before.dev_read_bytes,
    after.requested_flush_lines - before.requested_flush_lines, batch_order);
  if (!fresh && (unlink(pm_path) != 0 || unlink(xv_path) != 0)) {
    printf("PMFS_PERF_FAIL stage=cleanup\n");
    exit(1);
  }
  printf("PMFS_PERF_PASS mode=%s size=%d samples=%d warmup=%d\n", mode, size,
         samples, WARMUP);
  exit(0);
}
