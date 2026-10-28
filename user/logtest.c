// End-to-end native xv6 log recovery oracle. Use only on disposable media.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// A single write spans three data blocks but fits one xv6 log transaction.
#define VALUE_LEN 2185
#define GUARD_LEN 1091

static char data[VALUE_LEN];
static char expected[VALUE_LEN];

static void __attribute__((noreturn))
fail(int code)
{
  printf("XV6_LOG_TEST_FAIL code=%d\n", code);
  exit(1);
}

static void
pattern(char *buf, int len, int seed)
{
  for (int i = 0; i < len; i++)
    buf[i] = (i * 37 + seed) & 255;
}

static void
create_file(const char *name, int len, int seed)
{
  int fd = open(name, O_CREATE | O_TRUNC | O_RDWR);
  if (fd < 0)
    fail(1);
  pattern(data, len, seed);
  if (write(fd, data, len) != len || close(fd) != 0)
    fail(2);
}

static void
check_file(const char *name, int len, int seed, int code)
{
  struct stat st;
  char extra;
  int fd = open(name, O_RDONLY);
  if (fd < 0 || fstat(fd, &st) != 0 || st.size != (uint)len)
    fail(code);
  pattern(expected, len, seed);
  if (read(fd, data, len) != len || memcmp(data, expected, len) != 0 ||
      read(fd, &extra, 1) != 0 || close(fd) != 0)
    fail(code);
}

int
main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "disabled") == 0) {
    if (xv6_log_fault(2) != -1)
      fail(90);
    printf("XV6_LOG_DISABLED_PASS\n");
    exit(0);
  }
  if (argc == 3 && strcmp(argv[1], "crash") == 0) {
    int stage = atoi(argv[2]);
    if (stage < 1 || stage > 5 || xv6_log_fault(0) != -1)
      fail(3);
    create_file("logguard", GUARD_LEN, 41);
    create_file("logvalue", VALUE_LEN, 11);
    int fd = open("logvalue", O_RDWR);
    if (fd < 0)
      fail(4);
    pattern(data, VALUE_LEN, 79);
    printf("XV6_LOG_BASELINE_READY\n");
    if (xv6_log_fault(stage) != 0)
      fail(5); // instrumentation unavailable or another operation is active
    if (write(fd, data, VALUE_LEN) != VALUE_LEN)
      fail(6);
    fail(7); // the selected cut point must stop the write before it returns
  }
  if (argc == 3 && strcmp(argv[1], "check") == 0 &&
      (strcmp(argv[2], "old") == 0 || strcmp(argv[2], "new") == 0)) {
    check_file("logguard", GUARD_LEN, 41, 11);
    check_file("logvalue", VALUE_LEN, strcmp(argv[2], "new") == 0 ? 79 : 11,
               12);
    printf("XV6_LOG_TEST_PASS state=%s\n", argv[2]);
    exit(0);
  }
  fail(99);
}
