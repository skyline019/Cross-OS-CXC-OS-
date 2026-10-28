// End-to-end oracle for the dedicated PMFS API. Use only on disposable media.
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "user/user.h"

#define OLD       "version-one"
#define NEW       "version-two-after-sync"
#define ABANDON   "previous-stable"
#define KEY_LONG  0x1001UL
#define KEY_SHORT 0x1002UL

static char data[1024];
static char result[1024];

static void __attribute__((noreturn))
fail(const char *mode, int code)
{
  printf("PMFS_REGRESS_FAIL mode=%s code=%d\n", mode, code);
  exit(1);
}

#define CHECK(c, n)                                                            \
  do {                                                                         \
    if (!(c))                                                                  \
      fail(mode, (n));                                                         \
  } while (0)

static void
pattern(char *p, int n, int seed)
{
  int i;
  for (i = 0; i < n; i++)
    p[i] = (char)((i * 37 + seed) & 255);
}

static int
write_file(const char *path, const char *p, int n)
{
  int fd = pmfs_fopen(path);
  if (fd < 0)
    return -1;
  if (pmfs_fwrite(fd, p, n) != n || pmfs_fsync(fd) != 0 || pmfs_fclose(fd) != 0)
    return -1;
  return 0;
}

static int
check_file(const char *path, const char *p, int n)
{
  int fd, i;
  unsigned int len = 0, size = 0, type = 0;
  if (pmfs_fstat(path, &size, &type) != 0 || size != (unsigned)n || type != 1)
    return -1;
  fd = pmfs_fopen(path);
  if (fd < 0)
    return -1;
  // Short reads are exact prefixes rather than missing or silently skipped chunks.
  if (n > 3 && (pmfs_fread(fd, result, 3, &len) != 1 || len != 3 ||
                memcmp(result, p, 3) != 0))
    return -1;
  for (i = 0; i < 2; i++) {
    if (pmfs_fread(fd, result, sizeof(result), &len) != 1 ||
        len != (unsigned)n || memcmp(result, p, n) != 0)
      return -1;
  }
  return pmfs_fclose(fd);
}

static int
check_common(int a_new, int updated)
{
  const char *s = a_new ? NEW : OLD;
  unsigned int len = 0;
  int i;
  if (check_file("/t/a", s, strlen(s)) != 0 ||
      check_file("/t/abandon", ABANDON, strlen(ABANDON)) != 0)
    return -1;
  pattern(data, 512, updated ? 29 : 11);
  for (i = 0; i < 2; i++) {
    if (pmfs_get(KEY_LONG, result, sizeof(result) / 2, &len) != 1 ||
        len != 512 || memcmp(data, result, 512) != 0)
      return -2;
  }
  s = updated ? "kv-new" : "kv-old";
  if (pmfs_get(KEY_SHORT, result, 512, &len) != 1 || len != strlen(s) ||
      memcmp(result, s, len) != 0)
    return -3;
  if (updated) {
    if (pmfs_fopen("/t/delete") >= 0 ||
        check_file("/t/new", "new-file", 8) != 0)
      return -4;
  } else if (check_file("/t/delete", "delete-me", 9) != 0 ||
             pmfs_fopen("/t/new") >= 0) {
    return -5;
  }
  return 0;
}

static void
init_image(const char *mode)
{
  CHECK(pmfs_format() == 0, 1);
  CHECK(pmfs_format() < 0, 2); // already mounted: never format implicitly
  CHECK(pmfs_mkdir("/t") == 0, 3);
  CHECK(pmfs_create("/t/a") == 0 && write_file("/t/a", OLD, strlen(OLD)) == 0,
        4);
  CHECK(pmfs_create("/t/delete") == 0 &&
          write_file("/t/delete", "delete-me", 9) == 0,
        5);
  CHECK(pmfs_create("/t/abandon") == 0 &&
          write_file("/t/abandon", ABANDON, strlen(ABANDON)) == 0,
        6);
  pattern(data, 512, 11);
  CHECK(pmfs_put(KEY_LONG, data, 512) == 0 &&
          pmfs_put(KEY_SHORT, "kv-old", 6) == 0 && pmfs_seal() == 0 &&
          pmfs_seal() == 0,
        7); // identical roots in both slots
  CHECK(check_common(0, 0) == 0, 8);
}

static void
update_image(const char *mode)
{
  CHECK(check_common(0, 0) == 0, 10);
  CHECK(write_file("/t/a", NEW, strlen(NEW)) == 0, 11);
  pattern(data, 512, 29);
  CHECK(pmfs_put(KEY_LONG, data, 512) == 0 &&
          pmfs_put(KEY_SHORT, "kv-new", 6) == 0 && pmfs_seal() == 0,
        12);
  CHECK(pmfs_funlink("/t/delete") == 0, 13);
  CHECK(pmfs_create("/t/new") == 0 && write_file("/t/new", "new-file", 8) == 0,
        14);
  CHECK(check_common(1, 1) == 0, 15);
}

static void
api_edges(const char *mode)
{
  int fd, pid, status, i;
  unsigned int len = 99;
  CHECK(pmfs_put(0xc000000000000001UL, "bad", 3) < 0, 20);
  CHECK(pmfs_get(0xc000000000000001UL, result, 512, &len) < 0, 21);
  CHECK(pmfs_get(KEY_LONG, result, 16, &len) < 0, 22);
  CHECK(pmfs_create("relative") < 0 && pmfs_create("/t/../x") < 0 &&
          pmfs_create("/t//x") < 0 && pmfs_create("/t/") < 0,
        23);
  CHECK(pmfs_readdir("/t", result, 2) < 0 && pmfs_readdir("/t", result, 0) < 0,
        24);
  CHECK(pmfs_funlink("/t") < 0, 25);
  fd = pmfs_fopen("/t/a");
  CHECK(fd >= 0 && pmfs_fopen("/t/a") < 0 && pmfs_funlink("/t/a") < 0, 26);
  pid = fork();
  CHECK(pid >= 0, 27);
  if (pid == 0) {
    if (pmfs_fread(fd, result, sizeof(result), &len) >= 0 ||
        pmfs_fclose(fd) >= 0)
      exit(1);
    exit(0);
  }
  CHECK(wait(&status) == pid && status == 0 && pmfs_fclose(fd) == 0, 28);
  // Repeated process exits discard dirty chunks and release process-owned slots.
  for (i = 0; i < 40; i++) {
    pid = fork();
    CHECK(pid >= 0, 29);
    if (pid == 0) {
      fd = pmfs_fopen("/t/abandon");
      pattern(data, 200, i);
      if (fd < 0 || pmfs_fwrite(fd, data, 200) != 200)
        exit(1);
      exit(0); // deliberately do not call fsync/close
    }
    CHECK(wait(&status) == pid && status == 0, 30);
  }
  CHECK(check_file("/t/abandon", ABANDON, strlen(ABANDON)) == 0, 31);
}

static void
parallel_test(const char *mode, int populate)
{
  int i, pid, status;
  for (i = 0; populate && i < 4; i++) {
    pid = fork();
    CHECK(pid >= 0, 40);
    if (pid == 0) {
      char path[] = "/t/c0";
      char val[] = "child-0";
      int fd;
      unsigned int len;
      path[4] += i;
      val[6] += i;
      if (pmfs_create(path) != 0 || (fd = pmfs_fopen(path)) < 0 ||
          pmfs_fwrite(fd, val, 7) != 7 || pmfs_fsync(fd) != 0 ||
          pmfs_fclose(fd) != 0 || pmfs_put(0x2000UL + i, val, 7) != 0 ||
          pmfs_seal() != 0 || pmfs_get(0x2000UL + i, result, 512, &len) != 1 ||
          len != 7 || memcmp(result, val, 7) != 0)
        exit(1);
      exit(0);
    }
  }
  for (i = 0; populate && i < 4; i++)
    CHECK(wait(&status) > 0 && status == 0, 41);
  for (i = 0; i < 4; i++) {
    char path[] = "/t/c0";
    char val[] = "child-0";
    unsigned int len;
    path[4] += i;
    val[6] += i;
    CHECK(check_file(path, val, 7) == 0, 42);
    CHECK(pmfs_get(0x2000UL + i, result, 512, &len) == 1 && len == 7 &&
            memcmp(result, val, 7) == 0,
          43);
  }
}

static void
index_test(const char *mode, int populate)
{
  int i;
  unsigned int len;
  if (populate) {
    for (i = 0; i < 576; i++) {
      char val[4];
      val[0] = (char)i;
      val[1] = (char)(i >> 8);
      val[2] = 'k';
      val[3] = '!';
      CHECK(pmfs_put(0x10000UL + i, val, sizeof(val)) == 0, 50);
      if (i % 32 == 31)
        CHECK(pmfs_seal() == 0, 51);
    }
  }
  for (i = 0; i < 576; i++) {
    CHECK(pmfs_get(0x10000UL + i, result, 512, &len) == 1 && len == 4 &&
            (unsigned char)result[0] == (i & 255) &&
            (unsigned char)result[1] == (i >> 8) && result[2] == 'k' &&
            result[3] == '!',
          52);
  }
}

static void
capacity_test(const char *mode)
{
  int i, failed = 0;
  unsigned int len = 0;
  for (i = 0; i < 300; i++) {
    CHECK(pmfs_put(0x777UL, "latest", 6) == 0, 60);
    if (pmfs_seal() != 0) {
      failed = 1;
      break;
    }
  }
  CHECK(failed && i > 0, 61);
  CHECK(pmfs_get(KEY_SHORT, result, 512, &len) == 1 && len == 6 &&
          memcmp(result, "kv-old", 6) == 0,
        62);
  // Both fd and dedicated APIs must report a full-device sync/close failure
  // without replacing the last committed version with the dirty buffer.
  {
    int fd = open("/pm/t/a", O_RDWR);
    CHECK(fd >= 0 && write(fd, "x", 1) == 1 && fsync(fd) < 0 && sync() < 0 &&
            close(fd) < 0,
          63);
    CHECK(check_file("/t/a", OLD, strlen(OLD)) == 0, 64);
    fd = pmfs_fopen("/t/a");
    CHECK(fd >= 0 && pmfs_fwrite(fd, "x", 1) == 1 && pmfs_fsync(fd) < 0 &&
            pmfs_fclose(fd) < 0,
          65);
    // Exit cleanup discards the uncommitted dedicated handle.
  }
}

static void
fd_check(const char *mode)
{
  int fd, n, got = 0;
  struct stat st;
  fd = open("/pm/t/native", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.type == T_FILE && st.size == 12,
        80);
  while ((n = read(fd, result + got, 3)) > 0)
    got += n;
  CHECK(n == 0 && got == 12 && memcmp(result, "native-final", 12) == 0, 81);
  CHECK(close(fd) == 0, 82);
  fd = open("/pm/t/big", O_RDONLY);
  CHECK(fd >= 0, 83);
  got = 0;
  while ((n = read(fd, result + got, 101)) > 0)
    got += n;
  pattern(data, 1024, 71);
  CHECK(n == 0 && got == 1024 && memcmp(result, data, 1024) == 0 &&
          close(fd) == 0,
        84);
}

static void
fd_write_test(const char *mode)
{
  int fd, other, child, status, i;
  unsigned int len;
  struct stat st;
  CHECK(mkdir("/pm/t/sub") == 0, 85);
  fd = open("/pm/t/sub", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.type == T_DIR && close(fd) == 0 &&
          chdir("/pm/t") == 0 && chdir("/") == 0,
        86);
  CHECK(link("/pm/t/a", "block-link") < 0 && link("README", "/pm/t/link") < 0,
        87);
  fd = open("/pm/t/native", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, "hello", 5) == 5 && write(fd, " world", 6) == 6,
        88);
  CHECK(fstat(fd, &st) == 0 && st.type == T_FILE && st.size == 11, 89);
  for (i = 0; i < 8; i++)
    CHECK(pmfs_fread(i, result, sizeof(result), &len) < 0, 90);
  CHECK(fsync(fd) == 0 && close(fd) == 0, 91);

  fd = open("/pm/t/native", O_RDWR);
  CHECK(fd >= 0 && (other = dup(fd)) >= 0, 92);
  CHECK(read(fd, result, 3) == 3 && memcmp(result, "hel", 3) == 0, 93);
  CHECK(read(other, result, 4) == 4 && memcmp(result, "lo w", 4) == 0, 94);
  child = fork();
  CHECK(child >= 0, 95);
  if (child == 0) {
    if (read(fd, result, 2) != 2 || memcmp(result, "or", 2) != 0)
      exit(1);
    exit(0);
  }
  CHECK(wait(&status) == child && status == 0, 96);
  CHECK(read(fd, result, 2) == 2 && memcmp(result, "ld", 2) == 0, 97);
  CHECK(close(other) == 0 && close(fd) == 0, 98);

  fd = open("/pm/t/native", O_WRONLY);
  CHECK(fd >= 0 && write(fd, "HEY", 3) == 3 && close(fd) == 0, 100);
  fd = open("/pm/t/native", O_RDONLY);
  CHECK(fd >= 0 && read(fd, result, 32) == 11 &&
          memcmp(result, "HEYlo world", 11) == 0 && close(fd) == 0,
        101);
  fd = open("/pm/t/native", O_WRONLY | O_TRUNC);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.size == 0 &&
          write(fd, "native-final", 12) == 12 && fsync(fd) == 0 &&
          close(fd) == 0,
        102);

  child = fork();
  CHECK(child >= 0, 103);
  if (child == 0) {
    fd = open("/pm/t/exit", O_CREATE | O_WRONLY);
    if (fd < 0 || write(fd, "exit-flush", 10) != 10)
      exit(1);
    exit(0); // last reference closes and flushes on exit
  }
  CHECK(wait(&status) == child && status == 0, 104);
  fd = open("/pm/t/exit", O_RDONLY);
  CHECK(fd >= 0 && read(fd, result, 20) == 10 &&
          memcmp(result, "exit-flush", 10) == 0 && close(fd) == 0 &&
          unlink("/pm/t/exit") == 0,
        105);

  pattern(data, 1024, 71);
  fd = open("/pm/t/big", O_CREATE | O_WRONLY);
  CHECK(fd >= 0 && write(fd, data, 1024) == 1024 && write(fd, "x", 1) < 0 &&
          fsync(fd) == 0 && close(fd) == 0,
        106);
  fd_check(mode);
}

int
main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "";
  if (strcmp(mode, "init") == 0)
    init_image(mode);
  else if (strcmp(mode, "old") == 0) {
    CHECK(check_common(0, 0) == 0, 70);
  } else if (strcmp(mode, "anew") == 0) {
    CHECK(check_common(1, 0) == 0, 71);
  } else if (strcmp(mode, "update") == 0)
    update_image(mode);
  else if (strcmp(mode, "new") == 0) {
    CHECK(check_common(1, 1) == 0, 72);
  } else if (strcmp(mode, "edges") == 0)
    api_edges(mode);
  else if (strcmp(mode, "parallel") == 0)
    parallel_test(mode, 1);
  else if (strcmp(mode, "parallel-read") == 0)
    parallel_test(mode, 0);
  else if (strcmp(mode, "index-fill") == 0)
    index_test(mode, 1);
  else if (strcmp(mode, "index-read") == 0)
    index_test(mode, 0);
  else if (strcmp(mode, "capacity") == 0)
    capacity_test(mode);
  else if (strcmp(mode, "fd-write") == 0)
    fd_write_test(mode);
  else if (strcmp(mode, "fd-check") == 0)
    fd_check(mode);
  else if (strcmp(mode, "fd-remove") == 0) {
    CHECK(unlink("/pm/t/native") == 0 && open("/pm/t/native", O_RDONLY) < 0,
          107);
  } else if (strcmp(mode, "fd-absent") == 0) {
    CHECK(open("/pm/t/native", O_RDONLY) < 0, 108);
  } else if (strcmp(mode, "fd-global") == 0) {
    int fd = open("/pm/t/global", O_CREATE | O_WRONLY);
    CHECK(fd >= 0 && write(fd, "global-sync", 11) == 11 && sync() == 0, 109);
    printf("PMFS_REGRESS_GLOBAL_READY\n");
    for (;;)
      pause(100);
  } else if (strcmp(mode, "fd-global-check") == 0) {
    int fd = open("/pm/t/global", O_RDONLY);
    CHECK(fd >= 0 && read(fd, result, 20) == 11 &&
            memcmp(result, "global-sync", 11) == 0 && close(fd) == 0,
          110);
  } else if (strcmp(mode, "dirty") == 0) {
    int fd = pmfs_fopen("/t/a");
    CHECK(fd >= 0 && pmfs_fwrite(fd, "not-committed", 13) == 13 &&
            pmfs_put(KEY_SHORT, "not-committed", 13) == 0,
          73);
    printf("PMFS_REGRESS_DIRTY_READY\n");
    for (;;)
      pause(100);
  } else if (strcmp(mode, "fault") == 0 && argc == 4) {
    int fd = pmfs_fopen("/t/a");
    CHECK(fd >= 0 && pmfs_fwrite(fd, NEW, strlen(NEW)) == (int)strlen(NEW), 74);
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 75);
    pmfs_fsync(fd);
    fail(mode, 76); // fault-injection build must stop before this point
  } else
    fail(mode, 99);
  printf("PMFS_REGRESS_PASS mode=%s\n", mode);
  exit(0);
}
