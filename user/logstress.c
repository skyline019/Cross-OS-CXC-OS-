#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Exercise concurrent block-log writers and verify their complete files.
// Use only on disposable media: the named files are truncated.
#define BUFSZ  2000
#define WRITES 20

static char buf[BUFSZ];

static void __attribute__((noreturn))
fail(const char *file, const char *operation)
{
  printf("LOGSTRESS_FAIL file=%s operation=%s\n", file, operation);
  exit(1);
}

int
main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: logstress file ...\n");
    exit(1);
  }
  for (int i = 1; i < argc; i++) {
    int pid = fork();
    if (pid < 0)
      fail(argv[i], "fork");
    if (pid == 0) {
      int fd = open(argv[i], O_CREATE | O_TRUNC | O_RDWR);
      if (fd < 0)
        fail(argv[i], "open-write");
      memset(buf, '0' + i, BUFSZ);
      for (int j = 0; j < WRITES; j++)
        if (write(fd, buf, BUFSZ) != BUFSZ)
          fail(argv[i], "write");
      if (close(fd) != 0)
        fail(argv[i], "close-write");
      fd = open(argv[i], O_RDONLY);
      if (fd < 0)
        fail(argv[i], "open-read");
      for (int j = 0; j < WRITES; j++) {
        if (read(fd, buf, BUFSZ) != BUFSZ)
          fail(argv[i], "read");
        for (int k = 0; k < BUFSZ; k++)
          if (buf[k] != '0' + i)
            fail(argv[i], "data");
      }
      if (read(fd, buf, 1) != 0 || close(fd) != 0)
        fail(argv[i], "eof");
      exit(0);
    }
  }
  int failed = 0;
  for (int i = 1; i < argc; i++) {
    int status;
    if (wait(&status) < 0 || status != 0)
      failed = 1;
  }
  if (failed)
    fail("-", "child");
  printf("LOGSTRESS_PASS files=%d bytes=%d\n", argc - 1,
         (argc - 1) * WRITES * BUFSZ);
  exit(0);
}
