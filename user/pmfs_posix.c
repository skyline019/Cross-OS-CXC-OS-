// Thin file-like API over PMFS-KV (not a kernel VFS).
// path → FNV key → put/get/seal  → ONBOX_PMFS_POSIX_PASS
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static unsigned long
hash_path(const char *s)
{
  unsigned long h = 1469598103934665603ULL;
  while(*s){
    h ^= (unsigned char)(*s++);
    h *= 1099511628211ULL;
  }
  return h;
}

// Write whole "file" body under path key.
static int
pmfs_file_put(const char *path, const char *data, int len)
{
  unsigned long k = hash_path(path);
  if(pmfs_put(k, data, len) != 0)
    return -1;
  return pmfs_seal();
}

static int
pmfs_file_get(const char *path, char *dst, int cap, unsigned int *out)
{
  return pmfs_get(hash_path(path), dst, cap, out);
}

int
main(int argc, char *argv[])
{
  char buf[64];
  unsigned int len = 0;
  int hit, ops = 0;

  (void)argc;
  (void)argv;

  if(pmfs_file_put("/demo/a.txt", "hello-posix", 11) != 0){
    printf("ONBOX_PMFS_POSIX_FAIL stage=POSIX code=1\n");
    exit(1);
  }
  ops++;

  hit = pmfs_file_get("/demo/a.txt", buf, sizeof(buf), &len);
  if(hit != 1 || len != 11 || buf[0] != 'h'){
    printf("ONBOX_PMFS_POSIX_FAIL stage=POSIX code=2\n");
    exit(1);
  }
  ops++;

  if(pmfs_file_put("/demo/b.txt", "world", 5) != 0){
    printf("ONBOX_PMFS_POSIX_FAIL stage=POSIX code=3\n");
    exit(1);
  }
  ops++;

  hit = pmfs_file_get("/demo/b.txt", buf, sizeof(buf), &len);
  if(hit != 1 || len != 5){
    printf("ONBOX_PMFS_POSIX_FAIL stage=POSIX code=4\n");
    exit(1);
  }
  ops++;

  hit = pmfs_file_get("/demo/missing", buf, sizeof(buf), &len);
  if(hit != 0){
    printf("ONBOX_PMFS_POSIX_FAIL stage=POSIX code=5\n");
    exit(1);
  }
  ops++;

  printf("ONBOX_PMFS_POSIX_PASS ops=%d note=user_hash_kv_shim\n", ops);
  exit(0);
}
