// User-space PMFS KV probe → ONBOX_PMFS_SYSCALL_PASS
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char buf[64];
  unsigned int len = 0;
  int hit, ops = 0;

  (void)argc;
  (void)argv;

  if(pmfs_put(100, "hello", 5) != 0){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=1\n");
    exit(1);
  }
  ops++;
  if(pmfs_put(200, "world", 5) != 0){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=2\n");
    exit(1);
  }
  ops++;
  if(pmfs_seal() != 0){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=3\n");
    exit(1);
  }
  ops++;

  hit = pmfs_get(100, buf, sizeof(buf), &len);
  if(hit != 1 || len != 5 || buf[0] != 'h'){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=4\n");
    exit(1);
  }
  ops++;

  hit = pmfs_get(200, buf, sizeof(buf), &len);
  if(hit != 1 || len != 5 || buf[0] != 'w'){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=5\n");
    exit(1);
  }
  ops++;

  hit = pmfs_get(999, buf, sizeof(buf), &len);
  if(hit != 0){
    printf("ONBOX_PMFS_SYSCALL_FAIL stage=SYSCALL code=6\n");
    exit(1);
  }
  ops++;

  printf("ONBOX_PMFS_SYSCALL_PASS ops=%d\n", ops);
  exit(0);
}
