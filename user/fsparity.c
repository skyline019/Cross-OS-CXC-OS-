// Identical ordinary-API conformance probe for a supplied filesystem root.
// Run only on a fresh disposable filesystem; no backend-specific syscall is
// used. PASS here is necessary, not sufficient, for crash/persistence parity.
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/vfs.h"
#include "user/user.h"

#define PAGE_BYTES 4096
#define RECORD     4096
#define WORKERS    4
#define ROUNDS     12
#define REQUIRE(x)                                                             \
  do {                                                                         \
    if (!(x))                                                                  \
      return __LINE__;                                                         \
  } while (0)

static char data[RECORD];
static char readbuf[RECORD];

static int
makefile(const char *name, const char *bytes, int len)
{
  int fd = open(name, O_CREATE | O_TRUNC | O_RDWR);
  if (fd < 0)
    return -1;
  int ok = write(fd, bytes, len) == len && fsync(fd) == 0;
  if (close(fd) != 0)
    ok = 0;
  return ok ? 0 : -1;
}

static int
matches(const char *name, const char *bytes, int len)
{
  int fd = open(name, O_RDONLY);
  if (fd < 0)
    return 0;
  int n = read(fd, readbuf, len);
  int ok =
    n == len && memcmp(readbuf, bytes, len) == 0 && read(fd, readbuf, 1) == 0;
  if (close(fd) != 0)
    ok = 0;
  return ok;
}

static int
files(void)
{
  struct stat st;
  for (int i = 0; i < RECORD; i++)
    data[i] = (i * 31 + 17) & 255;
  int a = open("large", O_CREATE | O_TRUNC | O_RDWR);
  REQUIRE(a >= 0 && write(a, data, RECORD) == RECORD && fsync(a) == 0);
  int b = open("large", O_RDONLY);
  REQUIRE(b >= 0 && lseek(a, 2000, SEEK_SET) == 2000 &&
          write(a, "XYZ", 3) == 3 && read(b, readbuf, RECORD) == RECORD);
  data[2000] = 'X';
  data[2001] = 'Y';
  data[2002] = 'Z';
  REQUIRE(memcmp(data, readbuf, RECORD) == 0 && lseek(a, 0, SEEK_SET) == 0);
  int c = dup(a);
  REQUIRE(c >= 0 && read(c, readbuf, 1) == 1 && read(a, readbuf + 1, 1) == 1 &&
          memcmp(readbuf, data, 2) == 0);
  REQUIRE(makefile("other", "other", 5) == 0 && matches("other", "other", 5));
  int t = open("large", O_WRONLY | O_TRUNC);
  REQUIRE(t >= 0 && fstat(b, &st) == 0 && st.size == 0 &&
          write(t, "new", 3) == 3 && lseek(b, 0, SEEK_SET) == 0 &&
          read(b, readbuf, 3) == 3 && memcmp(readbuf, "new", 3) == 0);
  REQUIRE(fsync(t) == 0 && close(t) == 0 && close(c) == 0 && close(a) == 0 &&
          close(b) == 0);
  return 0;
}

static int
holes(void)
{
  struct stat st;
  int fd = open("hole", O_CREATE | O_TRUNC | O_RDWR);
  long last = MAXFILE * BSIZE - 1;
  REQUIRE(fd >= 0 && write(fd, "H", 1) == 1 &&
          lseek(fd, last, SEEK_SET) == last && write(fd, "Z", 1) == 1 &&
          fstat(fd, &st) == 0 && st.size == (uint64)last + 1);
  REQUIRE(lseek(fd, NDIRECT * BSIZE - 12, SEEK_SET) == NDIRECT * BSIZE - 12 &&
          read(fd, readbuf, 128) == 128);
  for (int i = 0; i < 128; i++)
    REQUIRE(readbuf[i] == 0);
  REQUIRE(lseek(fd, last - 1, SEEK_SET) == last - 1 &&
          read(fd, readbuf, 2) == 2 && readbuf[0] == 0 && readbuf[1] == 'Z' &&
          fsync(fd) == 0 && close(fd) == 0);
  return 0;
}

static int
links(void)
{
  struct stat a, b;
  int fd = open("file", O_CREATE | O_TRUNC | O_RDWR);
  REQUIRE(fd >= 0 && write(fd, "abc", 3) == 3 && link("file", "alias") == 0 &&
          fstat(fd, &a) == 0 && a.nlink == 2);
  int second = open("alias", O_RDWR);
  REQUIRE(second >= 0 && fstat(second, &b) == 0 && a.ino == b.ino &&
          unlink("file") == 0 && unlink("alias") == 0 &&
          open("file", O_RDONLY) < 0 && open("alias", O_RDONLY) < 0 &&
          fstat(fd, &a) == 0 && a.nlink == 0);
  REQUIRE(lseek(second, 1, SEEK_SET) == 1 && write(second, "Z", 1) == 1 &&
          lseek(fd, 0, SEEK_SET) == 0 && read(fd, readbuf, 3) == 3 &&
          memcmp(readbuf, "aZc", 3) == 0 && fsync(fd) == 0 &&
          close(second) == 0 && close(fd) == 0);
  return 0;
}

static int
directories(void)
{
  struct vfs_dirent ent;
  struct stat st;
  int seen = 0;
  REQUIRE(mkdir("dir") == 0 && mkdir("dir/sub") == 0 &&
          makefile("dir/file", "x", 1) == 0);
  REQUIRE(open("dir/missing/../file", O_RDONLY) < 0 &&
          open("dir/file/", O_RDONLY) < 0 &&
          matches("dir/sub/../file", "x", 1));
  int fd = open("dir", O_RDONLY);
  REQUIRE(fd >= 0 && fsync(fd) == 0);
  int n;
  while ((n = getdents(fd, &ent, sizeof(ent))) == sizeof(ent)) {
    if (strcmp(ent.name, ".") == 0) {
      REQUIRE(ent.type == T_DIR);
      seen |= 1;
    } else if (strcmp(ent.name, "..") == 0) {
      REQUIRE(ent.type == T_DIR);
      seen |= 2;
    } else if (strcmp(ent.name, "sub") == 0) {
      REQUIRE(ent.type == T_DIR);
      seen |= 4;
    } else if (strcmp(ent.name, "file") == 0) {
      REQUIRE(ent.type == T_FILE && stat("dir/file", &st) == 0 &&
              st.ino == ent.ino);
      seen |= 8;
    }
  }
  REQUIRE(n == 0 && seen == 15 && lseek(fd, 0, SEEK_SET) == 0);
  int copy = dup(fd);
  REQUIRE(copy >= 0 && getdents(copy, &ent, sizeof(ent)) == sizeof(ent) &&
          strcmp(ent.name, ".") == 0 &&
          getdents(fd, &ent, sizeof(ent)) == sizeof(ent) &&
          strcmp(ent.name, "..") == 0 && close(copy) == 0 && close(fd) == 0);
  return 0;
}

static int
rename_full(void)
{
  struct stat st;
  REQUIRE(mkdir("a") == 0 && mkdir("b") == 0 &&
          makefile("a/source", "new", 3) == 0 &&
          makefile("b/target", "old", 3) == 0);
  int old = open("b/target", O_RDONLY);
  REQUIRE(old >= 0 && rename("a/source", "b/target") == 0 &&
          open("a/source", O_RDONLY) < 0 && matches("b/target", "new", 3) &&
          fstat(old, &st) == 0 && st.nlink == 0 && read(old, readbuf, 3) == 3 &&
          memcmp(readbuf, "old", 3) == 0 && close(old) == 0);
  REQUIRE(link("b/target", "a/alias") == 0 &&
          rename("b/target", "a/alias") == 0 && matches("b/target", "new", 3) &&
          matches("a/alias", "new", 3));
  REQUIRE(mkdir("a/tree") == 0 && mkdir("a/tree/sub") == 0 &&
          makefile("a/tree/sub/file", "child", 5) == 0 &&
          rename("a/tree", "b/tree") == 0 &&
          matches("b/tree/sub/file", "child", 5) &&
          rename("b/tree", "b/tree/sub/loop") < 0 &&
          matches("b/tree/sub/file", "child", 5));
  REQUIRE(mkdir("a/empty") == 0 && rename("b/tree", "a/empty") == 0 &&
          matches("a/empty/sub/file", "child", 5));
  REQUIRE(mkdir("b/notempty") == 0 && makefile("b/notempty/x", "x", 1) == 0 &&
          rename("a/empty", "b/notempty") < 0 &&
          matches("a/empty/sub/file", "child", 5));
  REQUIRE(rename("a/empty", "a/alias") < 0 && rename("a/alias", "a/empty") < 0);
  return 0;
}

static int
long_path(void)
{
  char path[MAXPATH];
  int used = 0;
  for (int level = 0; level < 7; level++) {
    if (level)
      path[used++] = '/';
    for (int j = 0; j < 10; j++)
      path[used++] = 'a' + level;
    path[used] = 0;
    REQUIRE(mkdir(path) == 0);
  }
  path[used++] = '/';
  path[used++] = 'f';
  path[used] = 0;
  REQUIRE(makefile(path, "deep", 4) == 0 && matches(path, "deep", 4));
  return 0;
}

static int
long_name(void)
{
  const char *a = "abcdefghijklmn-one";
  const char *b = "abcdefghijklmn-two";
  REQUIRE(makefile(a, "A", 1) == 0 && makefile(b, "B", 1) == 0 &&
          matches(a, "A", 1) && matches(b, "B", 1));
  return 0;
}

static int
many_inodes(void)
{
  char name[] = "i00";
  for (int i = 0; i < 48; i++) {
    name[1] = '0' + i / 10;
    name[2] = '0' + i % 10;
    REQUIRE(makefile(name, "x", 1) == 0);
  }
  for (int i = 0; i < 48; i++) {
    name[1] = '0' + i / 10;
    name[2] = '0' + i % 10;
    REQUIRE(matches(name, "x", 1));
  }
  return 0;
}

static int
copy_fault(void)
{
  struct stat st;
  int fd = open("copy", O_CREATE | O_TRUNC | O_RDWR);
  REQUIRE(fd >= 0);
  uint64 old = (uint64)sbrk(0);
  int grow = (PAGE_BYTES - old % PAGE_BYTES) % PAGE_BYTES + PAGE_BYTES;
  REQUIRE(sbrk(grow) == (char *)old);
  char *edge = (char *)(old + grow - 64);
  memset(edge, 'w', 64);
  REQUIRE(write(fd, edge, 128) == 64 && fstat(fd, &st) == 0 && st.size == 64 &&
          lseek(fd, 0, SEEK_CUR) == 64);
  memset(data, 'r', 128);
  REQUIRE(lseek(fd, 0, SEEK_SET) == 0 && write(fd, data, 128) == 128 &&
          lseek(fd, 0, SEEK_SET) == 0 && read(fd, edge, 128) == 64 &&
          lseek(fd, 0, SEEK_CUR) == 64);
  for (int i = 0; i < 64; i++)
    REQUIRE(edge[i] == 'r');
  REQUIRE(close(fd) == 0 && sbrk(-grow) != SBRK_ERROR);
  return 0;
}

static int
append_atomic(void)
{
  int ready[2], start[2], status, counts[WORKERS] = {0};
  int fd = open("append", O_CREATE | O_TRUNC | O_RDWR);
  REQUIRE(fd >= 0 && close(fd) == 0 && pipe(ready) == 0 && pipe(start) == 0);
  for (int worker = 0; worker < WORKERS; worker++) {
    int pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
      char token = 'x';
      close(ready[0]);
      close(start[1]);
      int w = open("append", O_WRONLY | O_APPEND);
      token = w < 0 ? 'F' : 'R';
      if (write(ready[1], &token, 1) != 1 || read(start[0], &token, 1) != 1 ||
          w < 0)
        exit(1);
      memset(data, 'A' + worker, sizeof(data));
      for (int i = 0; i < ROUNDS; i++)
        if (write(w, data, sizeof(data)) != sizeof(data))
          exit(1);
      if (fsync(w) != 0 || close(w) != 0)
        exit(1);
      exit(0);
    }
  }
  close(ready[1]);
  close(start[0]);
  for (int i = 0; i < WORKERS; i++) {
    char token;
    REQUIRE(read(ready[0], &token, 1) == 1);
  }
  REQUIRE(write(start[1], "xxxx", WORKERS) == WORKERS);
  close(ready[0]);
  close(start[1]);
  for (int i = 0; i < WORKERS; i++)
    REQUIRE(wait(&status) > 0 && status == 0);
  fd = open("append", O_RDONLY);
  REQUIRE(fd >= 0);
  for (int i = 0; i < WORKERS * ROUNDS; i++) {
    REQUIRE(read(fd, readbuf, RECORD) == RECORD);
    int id = readbuf[0] - 'A';
    REQUIRE(id >= 0 && id < WORKERS);
    for (int j = 1; j < RECORD; j++)
      REQUIRE(readbuf[j] == readbuf[0]);
    counts[id]++;
  }
  for (int i = 0; i < WORKERS; i++)
    REQUIRE(counts[i] == ROUNDS);
  REQUIRE(read(fd, readbuf, 1) == 0 && close(fd) == 0);
  return 0;
}

static int
size_limit(void)
{
  struct stat st;
  long max = MAXFILE * BSIZE;
  int fd = open("limit", O_CREATE | O_TRUNC | O_RDWR);
  REQUIRE(fd >= 0 && lseek(fd, max - 4, SEEK_SET) == max - 4 &&
          write(fd, "abcdefgh", 8) == 4 && fstat(fd, &st) == 0 &&
          st.size == (uint64)max && lseek(fd, 0, SEEK_CUR) == max &&
          write(fd, "x", 1) < 0 && fstat(fd, &st) == 0 &&
          st.size == (uint64)max && fsync(fd) == 0 && close(fd) == 0);
  return 0;
}

static int
devices(void)
{
  struct stat st;
  REQUIRE(mknod("console", 1, 0) == 0);
  int fd = open("console", O_WRONLY);
  REQUIRE(fd >= 0 && fstat(fd, &st) == 0 && st.type == T_DEVICE &&
          close(fd) == 0 && link("console", "alias") == 0 &&
          unlink("console") == 0 && unlink("alias") == 0);
  return 0;
}

static int
exec_file(void)
{
  int src = open("/echo", O_RDONLY);
  int dst = open("exe", O_CREATE | O_TRUNC | O_WRONLY);
  REQUIRE(src >= 0 && dst >= 0);
  int n;
  while ((n = read(src, data, sizeof(data))) > 0)
    REQUIRE(write(dst, data, n) == n);
  REQUIRE(n == 0 && fsync(dst) == 0 && close(dst) == 0 && close(src) == 0);
  int pid = fork();
  REQUIRE(pid >= 0);
  if (pid == 0) {
    char *argv[] = {"exe", "FS_PARITY_EXEC_CHILD", 0};
    exec("exe", argv);
    exit(1);
  }
  int status;
  REQUIRE(wait(&status) == pid && status == 0);
  return 0;
}

static int
sync_calls(void)
{
  int fd = open("syncfile", O_CREATE | O_TRUNC | O_RDWR);
  int dir = open(".", O_RDONLY);
  REQUIRE(fd >= 0 && dir >= 0 && write(fd, "persist", 7) == 7 &&
          fsync(fd) == 0 && fsync(dir) == 0 && sync() == 0 && close(fd) == 0 &&
          close(dir) == 0 && matches("syncfile", "persist", 7));
  return 0;
}

// Release each case's namespace before the next one. This matters especially
// for a small legacy backend: capacity failures in one case must not silently
// turn all later cases into untested setup failures.
static int
remove_tree(const char *name)
{
  struct stat st;
  struct vfs_dirent ent;
  int fd = open(name, O_RDONLY);
  if (fd < 0 || fstat(fd, &st) != 0) {
    if (fd >= 0)
      close(fd);
    return -1;
  }
  if (st.type == T_DIR) {
    int n;
    while ((n = getdents(fd, &ent, sizeof(ent))) == sizeof(ent)) {
      if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
        continue;
      char child[MAXPATH];
      int prefix = strlen(name), suffix = strlen(ent.name);
      if (prefix + 1 + suffix >= (int)sizeof(child)) {
        close(fd);
        return -1;
      }
      memmove(child, name, prefix);
      child[prefix] = '/';
      memmove(child + prefix + 1, ent.name, suffix + 1);
      if (remove_tree(child) != 0) {
        close(fd);
        return -1;
      }
    }
    if (n != 0) {
      close(fd);
      return -1;
    }
  }
  if (close(fd) != 0)
    return -1;
  return unlink(name);
}

struct test {
  const char *id;
  int (*run)(void);
};

static struct test tests[] = {
  {"files", files},
  {"holes", holes},
  {"links", links},
  {"directories", directories},
  {"rename", rename_full},
  {"long_path", long_path},
  {"long_name", long_name},
  {"many_inodes", many_inodes},
  {"copy_fault", copy_fault},
  {"append_atomic", append_atomic},
  {"size_limit", size_limit},
  {"devices", devices},
  {"exec", exec_file},
  {"sync_calls", sync_calls},
  // Required extensions are deliberately reported as blocked, not silently
  // omitted or treated as passing while their ordinary syscalls are absent.
  {"symlink", 0},
  {"truncate_nonzero", 0},
  {"mmap", 0},
  {"permissions", 0},
  {"dynamic_mount", 0},
};

int
main(int argc, char **argv)
{
  if (argc != 2 || mkdir(argv[1]) != 0 || chdir(argv[1]) != 0) {
    printf("FS_PARITY_SETUP_FAIL\n");
    exit(1);
  }
  printf("FS_PARITY_BEGIN version=1 root=%s\n", argv[1]);
  int passed = 0, failed = 0, blocked = 0;
  for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    int rc = -1, status = -1;
    if (tests[i].run == 0) {
      blocked++;
      printf("FS_PARITY_CASE id=%s result=BLOCKED code=0\n", tests[i].id);
      continue;
    }
    if (mkdir(tests[i].id) == 0) {
      int pid = fork();
      if (pid == 0) {
        if (chdir(tests[i].id) != 0)
          exit(255);
        exit(tests[i].run());
      }
      if (pid > 0 && wait(&status) == pid)
        rc = status;
      if (remove_tree(tests[i].id) != 0 && rc == 0)
        rc = 254;
    }
    if (rc == 0)
      passed++;
    else
      failed++;
    printf("FS_PARITY_CASE id=%s result=%s code=%d\n", tests[i].id,
           rc == 0 ? "PASS" : "FAIL", rc);
  }
  printf("FS_PARITY_END total=%d pass=%d fail=%d blocked=%d\n",
         passed + failed + blocked, passed, failed, blocked);
  exit(failed != 0 || blocked != 0);
}
