#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PROBE_KEY 0x123456789abcdef0ULL
#define PROBE_PATH "/arena_save"

static const char kval[] = "arena-unique-kv-v1";
static const char fval[] = "arena-unique-file-v1";

int
main(int argc, char **argv)
{
  char kbuf[64], fbuf[64];
  uint klen = 0, flen = 0;
  int khit, fd, fhit = -9, kmatch, fmatch = 0;

  if(argc != 2){
    printf("PROBE_BAD_ARGS\n");
    exit(1);
  }
  if(strcmp(argv[1], "write") == 0){
    int create_rc, put_rc, seal_rc, n, sync_rc;
    put_rc = pmfs_put(PROBE_KEY, kval, sizeof(kval) - 1);
    seal_rc = pmfs_seal();
    create_rc = pmfs_create(PROBE_PATH);
    fd = pmfs_fopen(PROBE_PATH);
    if(fd < 0){
      printf("PROBE_WRITE_FAIL put=%d seal=%d create=%d open=%d\n", put_rc, seal_rc, create_rc, fd);
      exit(1);
    }
    n = pmfs_fwrite(fd, fval, sizeof(fval) - 1);
    sync_rc = pmfs_fsync(fd);
    pmfs_fclose(fd);
    fd = pmfs_fopen(PROBE_PATH);
    if(fd < 0){
      printf("PROBE_WRITE_FAIL reopen=%d\n", fd);
      exit(1);
    }
    fhit = pmfs_fread(fd, fbuf, sizeof(fbuf), &flen);
    pmfs_fclose(fd);
    khit = pmfs_get(PROBE_KEY, kbuf, sizeof(kbuf), &klen);
    kmatch = khit == 1 && klen == sizeof(kval) - 1 && memcmp(kbuf, kval, klen) == 0;
    fmatch = fhit == 1 && flen == sizeof(fval) - 1 && memcmp(fbuf, fval, flen) == 0;
    if(put_rc || seal_rc || create_rc || n != sizeof(fval) - 1 || sync_rc || !kmatch || !fmatch){
      printf("PROBE_WRITE_FAIL put=%d seal=%d create=%d n=%d sync=%d khit=%d klen=%d fhit=%d flen=%d\n", put_rc, seal_rc, create_rc, n, sync_rc, khit, klen, fhit, flen);
      exit(1);
    }
    printf("PROBE_WRITE_OK kvlen=%d filelen=%d sameboot=1 sync=0\n", klen, flen);
    exit(0);
  }
  if(strcmp(argv[1], "read") != 0){
    printf("PROBE_BAD_ARGS\n");
    exit(1);
  }
  khit = pmfs_get(PROBE_KEY, kbuf, sizeof(kbuf), &klen);
  kmatch = khit == 1 && klen == sizeof(kval) - 1 && memcmp(kbuf, kval, klen) == 0;
  fd = pmfs_fopen(PROBE_PATH);
  if(fd >= 0){
    fhit = pmfs_fread(fd, fbuf, sizeof(fbuf), &flen);
    fmatch = fhit == 1 && flen == sizeof(fval) - 1 && memcmp(fbuf, fval, flen) == 0;
    pmfs_fclose(fd);
  }
  printf("PROBE_READ_RESULT khit=%d klen=%d kmatch=%d fd=%d fhit=%d flen=%d fmatch=%d\n", khit, klen, kmatch, fd, fhit, flen, fmatch);
  if(!kmatch || !fmatch){
    printf("PROBE_READ_FAIL\n");
    exit(1);
  }
  printf("PROBE_READ_OK\n");
  exit(0);
}
