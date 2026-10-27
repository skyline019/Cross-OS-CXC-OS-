// Fixed-workload PMFS-KV vs xv6 fs: TRIALS medians.
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
run_pmfs_trial(int n, unsigned long key_base, char *payload)
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
  unsigned long pmfs_c[TRIALS], xv6_c[TRIALS];
  unsigned long t0, t1, med_p, med_x, ratio_x100;
  unsigned long key_base = 50000;
  unsigned long wa_ratio_x100;

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

  for(t = 0; t < TRIALS; t++){
    t0 = cycles();
    rc = run_pmfs_trial(n, key_base + (unsigned long)(t * n + n * 100), payload);
    if(rc != 0){
      printf("ONBOX_PMFS_BENCH_FAIL stage=BENCH code=%d\n", -rc);
      exit(1);
    }
    t1 = cycles();
    pmfs_c[t] = t1 - t0;
    if(pmfs_c[t] == 0)
      pmfs_c[t] = 1;

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

    printf("BENCH_TRIAL N=%d t=%d pmfs_cyc=%d xv6_cyc=%d\n",
           n, t, (int)pmfs_c[t], (int)xv6_c[t]);
  }

  pmfs_stat(&wa);
  med_p = median_u64(pmfs_c, TRIALS);
  med_x = median_u64(xv6_c, TRIALS);
  ratio_x100 = (med_x * 100) / med_p;
  wa_ratio_x100 = 0;
  if(wa.logical_bytes)
    wa_ratio_x100 = (wa.phys_bytes * 100) / wa.logical_bytes;

  printf("ONBOX_PMFS_BENCH_PASS trials=%d N=%d median_pmfs=%d median_xv6=%d ratio=%d logical=%d phys=%d wa_x100=%d\n",
         TRIALS, n, (int)med_p, (int)med_x, (int)ratio_x100,
         (int)wa.logical_bytes, (int)wa.phys_bytes, (int)wa_ratio_x100);
  exit(0);
}
