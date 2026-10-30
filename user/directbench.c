// Matched whole-file overwrite microbenchmark for the experimental direct
// core and an xv6/NVMe file. Not a comparison of equivalent VFS filesystems or
// physical-device durability. Run only with a disposable, formatted NVDIMM.
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/directcore.h"
#include "user/user.h"

enum { MAX_SAMPLES = 56, WARMUP = 4 };
static char payload[DIRECTCORE_MAX_SIZE], actual[DIRECTCORE_MAX_SIZE];

static void
pattern(int size, int generation)
{
  for (int i = 0; i < size; i++)
    payload[i] = (char)(i * 29 + (i >> 3) * 7 + generation * 31);
}

static int
update(int direct, int fd, int size)
{
  if (direct)
    return directcore(DIRECTCORE_WRITE, payload, size, 0);
  if (lseek(fd, 0, SEEK_SET) != 0 || write(fd, payload, size) != size ||
      fsync(fd) != 0)
    return -1;
  return 0;
}

static int
verify(int direct, int fd, int size)
{
  char extra;
  if (direct) {
    if (directcore(DIRECTCORE_READ, actual, sizeof(actual), 0) != size)
      return -1;
  } else if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, actual, size) != size ||
             read(fd, &extra, 1) != 0) {
    return -1;
  }
  return memcmp(actual, payload, size) == 0 ? 0 : -1;
}

static int
measure(int direct, int fd, int size, unsigned long *ticks)
{
  unsigned long start = cycles(), end;
  int rc = update(direct, fd, size);
  end = cycles();
  if (rc != 0 || end <= start || verify(direct, fd, size) != 0)
    return -1;
  *ticks = end - start;
  return 0;
}

static int
batch(int direct, int fd, int size, int count, unsigned long *ticks)
{
  unsigned long start = cycles(), end;
  for (int i = 0; i < count; i++) {
    pattern(size, i + 1000);
    if (update(direct, fd, size) != 0)
      return -1;
  }
  end = cycles();
  if (end <= start || verify(direct, fd, size) != 0)
    return -1;
  *ticks = end - start;
  return 0;
}

int
main(int argc, char **argv)
{
  int samples, size, order, fd;
  unsigned long dc, xv;
  const char *path = "/dbench";
  if (argc != 5 || strcmp(argv[1], "--force") != 0 ||
      (samples = atoi(argv[2])) < 1 || samples > MAX_SAMPLES ||
      ((size = atoi(argv[3])) != 64 && size != 512) ||
      ((order = atoi(argv[4])) != 0 && order != 1)) {
    printf("DIRECTBENCH_FAIL stage=args\n");
    exit(1);
  }
  if ((fd = open(path, O_RDONLY)) >= 0) {
    close(fd);
    printf("DIRECTBENCH_FAIL stage=existing-file\n");
    exit(1);
  }
  // Explicitly destructive, before timing: the harness supplies a fresh,
  // private NVDIMM for every boot. --force is mandatory even on blank media.
  if (directcore(DIRECTCORE_FORMAT, (void *)DIRECTCORE_FORMAT_CONFIRM, 0, 0) !=
        0 ||
      directcore(DIRECTCORE_READ, actual, sizeof(actual), 0) != 0 ||
      (fd = open(path, O_CREATE | O_RDWR)) < 0) {
    printf("DIRECTBENCH_FAIL stage=setup\n");
    exit(1);
  }
  pattern(size, -1);
  if (update(1, fd, size) != 0 || update(0, fd, size) != 0) {
    printf("DIRECTBENCH_FAIL stage=initial\n");
    exit(1);
  }
  for (int i = -WARMUP; i < samples; i++) {
    pattern(size, i);
    if ((i % 2 == 0 &&
         (measure(1, fd, size, &dc) != 0 || measure(0, fd, size, &xv) != 0)) ||
        (i % 2 != 0 &&
         (measure(0, fd, size, &xv) != 0 || measure(1, fd, size, &dc) != 0))) {
      printf("DIRECTBENCH_FAIL stage=sample i=%d\n", i);
      exit(1);
    }
    if (i >= 0)
      printf("DIRECTBENCH_SAMPLE size=%d i=%d direct_ticks=%lu xv_ticks=%lu "
             "order=%d\n",
             size, i, dc, xv, i % 2);
  }
  if ((order == 0 && (batch(1, fd, size, samples, &dc) != 0 ||
                      batch(0, fd, size, samples, &xv) != 0)) ||
      (order == 1 && (batch(0, fd, size, samples, &xv) != 0 ||
                      batch(1, fd, size, samples, &dc) != 0))) {
    printf("DIRECTBENCH_FAIL stage=batch\n");
    exit(1);
  }
  printf("DIRECTBENCH_BATCH size=%d count=%d direct_ticks=%lu xv_ticks=%lu "
         "order=%d\n",
         size, samples, dc, xv, order);
  if (close(fd) != 0 || unlink(path) != 0) {
    printf("DIRECTBENCH_FAIL stage=cleanup\n");
    exit(1);
  }
  printf("DIRECTBENCH_PASS size=%d samples=%d warmup=%d\n", size, samples,
         WARMUP);
  exit(0);
}
