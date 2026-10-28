// Matched operation-count smoke benchmark, NOT a physical PMEM performance claim.
// Usage: pmfs_bench [N], 1 <= N <= 32; ticks are xv6 r_time(), not cycles.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

enum { TRIALS = 5, VAL_LEN = 8, N_DEFAULT = 32, N_MAX = 32 };

struct pmfs_wa {
  unsigned long logical_bytes, phys_bytes, bloom_skips, fence_skips;
  unsigned long page_reads, splits, compacts, budget;
};

static void
mkname(char *b, int i)
{
  b[0] = 'b';
  b[1] = '/';
  b[2] = '0' + (i / 100) % 10;
  b[3] = '0' + (i / 10) % 10;
  b[4] = '0' + i % 10;
  b[5] = 0;
}

static void
mkpath(char *b, int i)
{
  b[0] = '/';
  b[1] = 'p';
  b[2] = 'm';
  b[3] = '/';
  b[4] = 'p';
  b[5] = '/';
  b[6] = '0' + (i / 100) % 10;
  b[7] = '0' + (i / 10) % 10;
  b[8] = '0' + i % 10;
  b[9] = 0;
}

static unsigned long
median(unsigned long *a)
{
  int i, j;
  unsigned long t;
  for (i = 0; i < TRIALS; i++)
    for (j = i + 1; j < TRIALS; j++)
      if (a[j] < a[i]) {
        t = a[i];
        a[i] = a[j];
        a[j] = t;
      }
  return a[TRIALS / 2];
}

static int
kv_trial(int n, unsigned long base, const char *payload)
{
  int i;
  char buf[VAL_LEN];
  unsigned int len;
  for (i = 0; i < n; i++)
    if (pmfs_put(base + i, payload, VAL_LEN) != 0)
      return -1;
  if (pmfs_seal() != 0)
    return -2;
  for (i = 0; i < n; i++)
    if (pmfs_get(base + i, buf, sizeof(buf), &len) != 1 || len != VAL_LEN ||
        memcmp(buf, payload, VAL_LEN) != 0)
      return -3;
  return 0;
}

// Each operation is checked, including the bytes read back. Both file paths
// perform N create/write/sync/close/reopen/read/close/unlink transactions.
static int
pmfile_trial(int n, const char *payload)
{
  int i, fd;
  char path[12], buf[VAL_LEN];
  for (i = 0; i < n; i++) {
    mkpath(path, i);
    if ((fd = open(path, O_CREATE | O_RDWR)) < 0 ||
        write(fd, payload, VAL_LEN) != VAL_LEN || fsync(fd) != 0 ||
        close(fd) != 0 || (fd = open(path, O_RDONLY)) < 0 ||
        read(fd, buf, sizeof(buf)) != VAL_LEN ||
        memcmp(buf, payload, VAL_LEN) != 0 || close(fd) != 0 ||
        unlink(path) != 0)
      return -1;
  }
  return 0;
}

static int
xv6_trial(int n, const char *payload)
{
  int i, fd;
  char path[8], buf[VAL_LEN];
  for (i = 0; i < n; i++) {
    mkname(path, i);
    if ((fd = open(path, O_CREATE | O_RDWR)) < 0 ||
        write(fd, payload, VAL_LEN) != VAL_LEN || fsync(fd) != 0 ||
        close(fd) != 0 || (fd = open(path, O_RDONLY)) < 0 ||
        read(fd, buf, sizeof(buf)) != VAL_LEN ||
        memcmp(buf, payload, VAL_LEN) != 0 || close(fd) != 0 ||
        unlink(path) != 0)
      return -1;
  }
  return 0;
}

static unsigned long
timed_file(int n, int is_pmfs, const char *payload)
{
  unsigned long start = cycles(), end;
  int rc = is_pmfs ? pmfile_trial(n, payload) : xv6_trial(n, payload);
  end = cycles();
  if (rc != 0)
    return 0;
  return end > start ? end - start : 1;
}

int
main(int argc, char *argv[])
{
  int t, n = argc >= 2 ? atoi(argv[1]) : N_DEFAULT;
  unsigned long kv[TRIALS], file[TRIALS], xv6[TRIALS], start, end;
  unsigned long med_kv, med_file, med_xv6, logical, phys, wa;
  struct pmfs_wa before, after;
  const char payload[VAL_LEN] = {'p', 'm', 'f', 's', 'd', 'a', 't', 'a'};

  if (n < 1 || n > N_MAX) {
    printf("ONBOX_PMFS_BENCH_FAIL stage=ARGS code=1 valid_N=1..32\n");
    exit(1);
  }
  // The directory may already exist; all individual benchmark names must not.
  mkdir("/pm/p");
  mkdir("b");
  if (pmfs_stat(&before) != 0) {
    printf("ONBOX_PMFS_BENCH_FAIL stage=STAT code=1\n");
    exit(1);
  }
  for (t = 0; t < TRIALS; t++) {
    start = cycles();
    if (kv_trial(n, 50000UL + (unsigned long)(t * n), payload) != 0) {
      printf("ONBOX_PMFS_BENCH_FAIL stage=KV code=%d\n", t);
      exit(1);
    }
    end = cycles();
    kv[t] = end > start ? end - start : 1;
    // Alternate order to reduce systematic first/second bias.
    if (t % 2 == 0) {
      file[t] = timed_file(n, 1, payload);
      xv6[t] = timed_file(n, 0, payload);
    } else {
      xv6[t] = timed_file(n, 0, payload);
      file[t] = timed_file(n, 1, payload);
    }
    if (file[t] == 0 || xv6[t] == 0) {
      printf("ONBOX_PMFS_BENCH_FAIL stage=FILE code=%d\n", t);
      exit(1);
    }
    printf(
      "BENCH_TRIAL N=%d t=%d kv=%d file=%d xv6=%d count_pmfs=%d count_xv6=%d\n",
      n, t, (int)kv[t], (int)file[t], (int)xv6[t], n, n);
  }
  if (pmfs_stat(&after) != 0) {
    printf("ONBOX_PMFS_BENCH_FAIL stage=STAT code=2\n");
    exit(1);
  }
  med_kv = median(kv);
  med_file = median(file);
  med_xv6 = median(xv6);
  logical = after.logical_bytes - before.logical_bytes;
  phys = after.phys_bytes - before.phys_bytes;
  wa = logical ? phys * 100 / logical : 0;
  // Rates are operations per million r_time ticks, not ops/s or CPU cycles.
  printf(
    "ONBOX_PMFS_BENCH_PASS trials=%d N=%d median_pmfs=%d median_file=%d median_xv6=%d ratio=%d fratio=%d thr_file=%d thr_xv6=%d logical=%d phys=%d wa_x100=%d count_pmfs=%d count_xv6=%d units=r_time_ticks\n",
    TRIALS, n, (int)med_kv, (int)med_file, (int)med_xv6,
    (int)(med_xv6 * 100 / med_kv), (int)(med_xv6 * 100 / med_file),
    (int)((unsigned long)n * 1000000UL / med_file),
    (int)((unsigned long)n * 1000000UL / med_xv6), (int)logical, (int)phys,
    (int)wa, n, n);
  exit(0);
}
