#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  if (argc != 2 || strcmp(argv[1], "--force") != 0) {
    fprintf(2, "usage: pmformat --force\n");
    fprintf(2, "WARNING: destroys the entire unmounted PMFS image.\n");
    exit(1);
  }
  if (pmfs_format() != 0) {
    fprintf(2, "pmformat: failed (mounted or device unavailable)\n");
    exit(1);
  }
  printf("pmformat: v3 ready\n");
  exit(0);
}
