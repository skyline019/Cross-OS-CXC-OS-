// PMFS file/KV vs xv6 fs: latency medians + throughput (ops/s).
// Usage: pmfs_bench [N]     default N=32, max 48
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

enum {
  TRIALS = 5,
  VAL_LEN = 8,
  N_DEFAULT = 32,
  N_MAX = 48,
};

struct pmfs_wa {
  unsigned long logical_bytes;
  unsigned long phys_bytes;
  unsigned long bloom_skips;
  unsigned long fence_skips;
  unsigned long page_reads;
  unsigned long splits;
  unsigned long compacts;
  unsigned long budget;
};

static void
mkname(char *b, int i)
{
  strcpy(b, "b");
  b[1] = '0' + (i / 100) % 10;
  b[2] = '0' + (i / 10) % 10;
  b[3] = '0' + i % 10;
  b[4] = 0;
}

static void
mkpath(char *b, int i)
{
  strcpy(b, "/p/");
  b[3] = '0' + (i / 100) % 10;
  b[4] = '0' + (i / 10) % 10;
  b[5] = '0' + i % 10;
  b[6] = 0;
}

static void
sort_u64(unsigned long *a, int n)
{
  int i, j;
  unsigned long t;
  for(i = 0; i < n; i++){
    for(j = i + 1; j < n; j++){
      if(a[j] < a[i]){
        t = a[i];
        a[i] = a[j];
        a[j] = t;
      }
    }
  }
}

static unsigned long
median_u64(unsigned long *a, int n)
{
  sort_u64(a, n);
  return a[n / 2];
}

static int
run_pmfs_kv_trial(int n, unsigned long key_base, char *payload)
{
  int i;
  unsigned int len = 0;

  for(i = 0; i < n; i++){
    if(pmfs_put(key_base + (unsigned long)i, payload, VAL_LEN) != 0)
      return -2;
  }
  if(pmfs_seal() != 0)
    return -3;
  for(i = 0; i < n; i++){
    if(pmfs_get(key_base + (unsigned long)i, payload, 16, &len) != 1)
      return -4;
  }
  return 0;
}

static int
run_pmfs_file_trial(int n, int name_base, char *payload)
{
  int i, fd;
  char path[12];
  unsigned int len = 0;

  for(i = 0; i < n; i++){
    mkpath(path, name_base + i);
    pmfs_funlink(path);
    if(pmfs_create(path) != 0)
      return -10;
    fd = pmfs_fopen(path);
    if(fd < 0)
      return -11;
    if(pmfs_fwrite(fd, payload, VAL_LEN) != VAL_LEN)
      return -12;
    if(pmfs_fsync(fd) != 0)
      return -13;
    if(pmfs_fread(fd, payload, 16, &len) != 1)
      return -14;
    if(pmfs_fclose(fd) != 0)
      return -15;
  }
  return 0;
}

static int
run_xv6_trial(int n, int name_base, char *payload)
{
  int i, fd;
  char name[8];

  for(i = 0; i < n; i++){
    mkname(name, name_base + i);
    unlink(name);
    fd = open(name, O_CREATE | O_RDWR);
    if(fd < 0)
      return -5;
    if(write(fd, payload, VAL_LEN) != VAL_LEN){
      close(fd);
      return -6;
    }
    close(fd);
  }
  sync();
  return 0;
}

int
main(int argc, char *argv[])
{
  int t, rc, n;
  char payload[16];
  struct pmfs_wa wa;
  unsigned long pmfs_c[TRIALS], file_c[TRIALS], xv6_c[TRIALS];
  unsigned long t0, t1, med_p, med_f, med_x, ratio_x100, fratio_x100;
  unsigned long key_base = 50000;
  unsigned long wa_ratio_x100;
  unsigned long thr_file, thr_xv6;

  n = N_DEFAULT;
  if(argc >= 2){
    n = atoi(argv[1]);
    if(n < 1)
      n = 1;
    if(n > N_MAX)
      n = N_MAX;
  }

  memset(payload, 'x', sizeof(payload));
  memset(&wa, 0, sizeof(wa));

  pmfs_mkdir("/p");

  for(t = 0; t < TRIALS; t++){
    t0 = cycles();
    rc = run_pmfs_kv_trial(n, key_base + (unsigned long)(t * n + n * 100), payload);
    if(rc != 0){
      printf("ONBOX_PMFS_BENCH_FAIL stage=BENCH code=%d\n", -rc);
      exit(1);
    }
    t1 = cycles();
    pmfs_c[t] = t1 - t0;
    if(pmfs_c[t] == 0)
      pmfs_c[t] = 1;

    t0 = cycles();
    // Reuse path namespace so inode table stays within PMFILE_MAX_INO.
    rc = run_pmfs_file_trial(n, 0, payload);
    if(rc != 0){
      printf("ONBOX_PMFS_BENCH_FAIL stage=BENCH code=%d\n", -rc);
      exit(1);
    }
    t1 = cycles();
    file_c[t] = t1 - t0;
    if(file_c[t] == 0)
      file_c[t] = 1;

    t0 = cycles();
    rc = run_xv6_trial(n, t * n, payload);
    if(rc != 0){
      printf("ONBOX_PMFS_BENCH_FAIL stage=BENCH code=%d\n", -rc);
      exit(1);
    }
    t1 = cycles();
    xv6_c[t] = t1 - t0;
    if(xv6_c[t] == 0)
      xv6_c[t] = 1;

    printf("BENCH_TRIAL N=%d t=%d kv=%d file=%d xv6=%d\n",
           n, t, (int)pmfs_c[t], (int)file_c[t], (int)xv6_c[t]);
  }

  pmfs_stat(&wa);
  med_p = median_u64(pmfs_c, TRIALS);
  med_f = median_u64(file_c, TRIALS);
  med_x = median_u64(xv6_c, TRIALS);
  ratio_x100 = (med_x * 100) / med_p;
  fratio_x100 = (med_x * 100) / med_f;
  wa_ratio_x100 = 0;
  if(wa.logical_bytes)
    wa_ratio_x100 = (wa.phys_bytes * 100) / wa.logical_bytes;

  // Throughput in relative ops/s units: N files / median_time * scale
  // (r_time units; report as ops_x1000_per_tick for machine parse)
  thr_file = (med_f > 0) ? ((unsigned long)n * 1000000ULL) / med_f : 0;
  thr_xv6 = (med_x > 0) ? ((unsigned long)n * 1000000ULL) / med_x : 0;

  printf("ONBOX_PMFS_BENCH_PASS trials=%d N=%d median_pmfs=%d median_file=%d median_xv6=%d ratio=%d fratio=%d thr_file=%d thr_xv6=%d logical=%d phys=%d wa_x100=%d\n",
         TRIALS, n, (int)med_p, (int)med_f, (int)med_x, (int)ratio_x100, (int)fratio_x100,
         (int)thr_file, (int)thr_xv6,
         (int)wa.logical_bytes, (int)wa.phys_bytes, (int)wa_ratio_x100);
  exit(0);
}
