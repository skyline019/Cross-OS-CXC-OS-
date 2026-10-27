// Userland smoke for PMEM file API → ONBOX_PMFS_FILEUSER_PASS
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int fd;
  char buf[64];
  unsigned int len = 0;

  (void)argc;
  (void)argv;

  pmfs_funlink("/u/a.txt");
  if(pmfs_create("/u/a.txt") != 0){
    printf("ONBOX_PMFS_FILEUSER_FAIL code=1\n");
    exit(1);
  }
  fd = pmfs_fopen("/u/a.txt");
  if(fd < 0){
    printf("ONBOX_PMFS_FILEUSER_FAIL code=2\n");
    exit(1);
  }
  if(pmfs_fwrite(fd, "file-api", 8) != 8){
    printf("ONBOX_PMFS_FILEUSER_FAIL code=3\n");
    exit(1);
  }
  if(pmfs_fsync(fd) != 0){
    printf("ONBOX_PMFS_FILEUSER_FAIL code=4\n");
    exit(1);
  }
  if(pmfs_fread(fd, buf, sizeof(buf), &len) != 1 || len != 8){
    printf("ONBOX_PMFS_FILEUSER_FAIL code=5\n");
    exit(1);
  }
  printf("ONBOX_PMFS_FILEUSER_PASS fd=%d len=%d\n", fd, len);
  exit(0);
}
