// Namespace, descriptor and COW rename tests. Run only on disposable media.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/vfs.h"
#include "user/user.h"

static char buf[1024];

static void __attribute__((noreturn))
fail(const char *mode, int code)
{
  printf("VFS_TEST_FAIL mode=%s code=%d\n", mode, code);
  exit(1);
}

#define CHECK(c, n)                                                            \
  do {                                                                         \
    if (!(c))                                                                  \
      fail(mode, (n));                                                         \
  } while (0)

static int
writefile(const char *path, const char *s)
{
  int fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  int len = strlen(s);
  if (fd < 0)
    return -1;
  if (write(fd, s, len) != len || fsync(fd) != 0 || close(fd) != 0)
    return -1;
  return 0;
}

static int
checkfile(const char *path, const char *s)
{
  int fd = open(path, O_RDONLY);
  int n, len = strlen(s);
  if (fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf));
  if (n != len || memcmp(buf, s, len) != 0 || close(fd) != 0)
    return -1;
  return 0;
}

static void
basic(const char *mode)
{
  int fd, fd2, n, i, j, pid, status, ready[2], go[2], fds[16];
  int dots, dotdots, dirs, longs, mounts, count[3];
  struct stat st;
  struct vfs_dirent ent;
  struct dirent de;

  fd = open("/", O_RDONLY);
  CHECK(fd >= 0, 1);
  mounts = 0;
  while ((n = getdents(fd, &ent, sizeof(ent))) == sizeof(ent))
    if (strcmp(ent.name, "pm") == 0)
      mounts++;
  CHECK(n == 0 && mounts == 1 && close(fd) == 0, 2);
  CHECK(mkdir("/pm/vf") == 0 && mkdir("/pm/vf/dir") == 0 &&
          mkdir("/pm/vf/dir/sub") == 0,
        3);
  CHECK(open("/pm/vf/no/../x", O_CREATE | O_WRONLY) < 0 &&
          open("/pm/vf/dir/missing/..", O_RDONLY) < 0,
        4);

  fd = open("/pm/vf/dir/f", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, "abc", 3) == 3 && lseek(fd, 10, SEEK_SET) == 10 &&
          write(fd, "Z", 1) == 1 && lseek(fd, 0, SEEK_SET) == 0,
        5);
  CHECK(read(fd, buf, 11) == 11 && memcmp(buf, "abc", 3) == 0 && buf[10] == 'Z',
        6);
  for (i = 3; i < 10; i++)
    CHECK(buf[i] == 0, 7);
  CHECK(lseek(fd, -12, SEEK_CUR) < 0 && lseek(fd, 0, 99) < 0 &&
          lseek(fd, 1025, SEEK_SET) < 0,
        8);
  CHECK(lseek(fd, -2, SEEK_END) == 9 && (fd2 = dup(fd)) >= 0 &&
          read(fd2, buf, 1) == 1 && buf[0] == 0 && read(fd, buf, 1) == 1 &&
          buf[0] == 'Z' && fsync(fd) == 0 && close(fd2) == 0 && close(fd) == 0,
        9);
  fd = open("/pm/vf/dir/f", O_RDWR | O_APPEND);
  CHECK(fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 && write(fd, "x", 1) == 1 &&
          lseek(fd, -2, SEEK_END) == 10 && read(fd, buf, 2) == 2 &&
          memcmp(buf, "Zx", 2) == 0 && close(fd) == 0,
        10);
  CHECK(open("/pm/vf/dir/f/..", O_RDONLY) < 0 &&
          open("/pm/vf/dir/f/", O_RDONLY) < 0 &&
          open("/pm/vf/dir/f", O_RDONLY | O_TRUNC) < 0,
        11);
  // Independent opens have separate offsets but share the inode's working
  // data, including dirty writes and truncation. Dedicated handles stay gated.
  fd = open("/pm/vf/dir/f", O_RDWR);
  fd2 = open("/pm/vf/dir/f", O_RDONLY);
  CHECK(fd >= 0 && fd2 >= 0 && write(fd, "Q", 1) == 1 &&
          read(fd2, buf, 3) == 3 && memcmp(buf, "Qbc", 3) == 0 &&
          pmfs_fopen("/vf/dir/f") < 0 && close(fd2) == 0,
        41);
  fd2 = open("/pm/vf/dir/f", O_WRONLY | O_TRUNC);
  CHECK(fd2 >= 0 && fstat(fd, &st) == 0 && st.size == 0 &&
          write(fd2, "re", 2) == 2 && lseek(fd, 0, SEEK_SET) == 0 &&
          read(fd, buf, 2) == 2 && memcmp(buf, "re", 2) == 0 &&
          close(fd2) == 0 && close(fd) == 0,
        42);

  fd = open("vfs-native", O_CREATE | O_RDWR | O_TRUNC);
  CHECK(fd >= 0 && write(fd, "abc", 3) == 3 && lseek(fd, 1, SEEK_SET) == 1 &&
          write(fd, "X", 1) == 1 && lseek(fd, 0, SEEK_SET) == 0 &&
          read(fd, buf, 3) == 3 && memcmp(buf, "aXc", 3) == 0 && close(fd) == 0,
        12);
  fd = open("vfs-native", O_RDWR | O_APPEND);
  CHECK(fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 && write(fd, "!", 1) == 1 &&
          lseek(fd, -1, SEEK_END) == 3 && read(fd, buf, 1) == 1 &&
          buf[0] == '!' && close(fd) == 0 && unlink("vfs-native") == 0,
        13);

  CHECK(writefile("/pm/vf/abcdefghijklmnopqr", "long") == 0, 14);
  fd = open("/pm/vf", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.type == T_DIR &&
          getdents(fd, &ent, sizeof(ent) - 1) < 0,
        15);
  dots = dotdots = dirs = longs = 0;
  while ((n = getdents(fd, &ent, sizeof(ent))) == sizeof(ent)) {
    dots += strcmp(ent.name, ".") == 0;
    dotdots += strcmp(ent.name, "..") == 0;
    dirs += strcmp(ent.name, "dir") == 0;
    longs += strcmp(ent.name, "abcdefghijklmnopqr") == 0;
  }
  CHECK(n == 0 && dots == 1 && dotdots == 1 && dirs == 1 && longs == 1, 16);
  CHECK(lseek(fd, 0, SEEK_SET) == 0 && (fd2 = dup(fd)) >= 0 &&
          getdents(fd, &ent, sizeof(ent)) == sizeof(ent) &&
          strcmp(ent.name, ".") == 0 &&
          getdents(fd2, &ent, sizeof(ent)) == sizeof(ent) &&
          strcmp(ent.name, "..") == 0 && close(fd2) == 0 &&
          lseek(fd, 0, SEEK_SET) == 0,
        17);
  while ((n = read(fd, &de, sizeof(de))) == sizeof(de))
    ;
  CHECK(n < 0 && close(fd) == 0, 18); // old 14-byte ABI must not truncate
  fd = open("/pm/vf/dir/f", O_RDONLY);
  CHECK(fd >= 0 && getdents(fd, &ent, sizeof(ent)) < 0 && close(fd) == 0, 19);

  CHECK(chdir("/pm//vf/./dir/sub") == 0, 20);
  fd = open("../../../../README", O_RDONLY);
  CHECK(fd >= 0 && close(fd) == 0 && open("missing/../f", O_RDONLY) < 0, 21);
  pid = fork();
  CHECK(pid >= 0, 22);
  if (pid == 0) {
    char *args[] = {"echo", 0};
    if (exec("echo", args) >= 0)
      exit(1); // must not fall back to native root for a PMFS-relative path
    exec("/echo", args);
    exit(2);
  }
  CHECK(wait(&status) == pid && status == 0 && chdir("/pm/..") == 0, 23);
  fd = open("pm/vf/dir/f", O_RDONLY);
  CHECK(fd >= 0 && close(fd) == 0, 24);

  CHECK(mkdir("/pm/vf/hold") == 0, 25);
  fd = open("/pm/vf/hold", O_RDONLY);
  CHECK(fd >= 0 && unlink("/pm/vf/hold") < 0 && close(fd) == 0, 26);
  CHECK(pipe(ready) == 0 && pipe(go) == 0, 27);
  pid = fork();
  CHECK(pid >= 0, 28);
  if (pid == 0) {
    close(ready[0]);
    close(go[1]);
    if (chdir("/pm/vf/hold") < 0 || write(ready[1], "r", 1) != 1 ||
        read(go[0], buf, 1) != 1)
      exit(1);
    exit(0);
  }
  close(ready[1]);
  close(go[0]);
  CHECK(read(ready[0], buf, 1) == 1 && unlink("/pm/vf/hold") < 0 &&
          write(go[1], "g", 1) == 1 && wait(&status) == pid && status == 0 &&
          unlink("/pm/vf/hold") == 0,
        29);
  close(ready[0]);
  close(go[1]);

  CHECK(mkdir("/pm/vf/a") == 0 && mkdir("/pm/vf/a/sub") == 0 &&
          writefile("/pm/vf/a/sub/item", "inside") == 0 && pipe(ready) == 0 &&
          pipe(go) == 0,
        30);
  pid = fork();
  CHECK(pid >= 0, 31);
  if (pid == 0) {
    close(ready[0]);
    close(go[1]);
    if (chdir("/pm/vf/a/sub") < 0 || write(ready[1], "r", 1) != 1 ||
        read(go[0], buf, 1) != 1 || checkfile("item", "inside") != 0 ||
        chdir("..") < 0 || checkfile("sub/item", "inside") != 0)
      exit(1);
    exit(0);
  }
  close(ready[1]);
  close(go[0]);
  CHECK(read(ready[0], buf, 1) == 1 && rename("/pm/vf/a", "/pm/vf/new") == 0 &&
          open("/pm/vf/a/sub/item", O_RDONLY) < 0 &&
          rename("/pm/vf/new", "/pm/vf/new/sub/no") < 0 &&
          write(go[1], "g", 1) == 1 && wait(&status) == pid && status == 0,
        32);
  close(ready[0]);
  close(go[1]);

  // Moving an open, dirty file preserves its inode, shared buffer and fd.
  fd = open("/pm/vf/live", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, "dirty", 5) == 5 &&
          rename("/pm/vf/live", "/pm/vf/moved") == 0 &&
          (fd2 = open("/pm/vf/moved", O_RDONLY)) >= 0 &&
          read(fd2, buf, 5) == 5 && memcmp(buf, "dirty", 5) == 0 &&
          fsync(fd) == 0 && close(fd2) == 0 && close(fd) == 0 &&
          checkfile("/pm/vf/moved", "dirty") == 0,
        56);
  CHECK(mkdir("/pm/vf/left") == 0 && mkdir("/pm/vf/target") == 0 &&
          mkdir("/pm/vf/target/k") == 0 &&
          rename("/pm/vf/left", "/pm/vf/target") < 0 &&
          unlink("/pm/vf/target/k") == 0,
        57);
  fd = open("/pm/vf/target", O_RDONLY);
  CHECK(fd >= 0 && rename("/pm/vf/left", "/pm/vf/target") < 0 &&
          close(fd) == 0 && rename("/pm/vf/left", "/pm/vf/target") == 0 &&
          open("/pm/vf/left", O_RDONLY) < 0,
        58);

  CHECK(writefile("/pm/vf/src", "source") == 0 &&
          writefile("/pm/vf/dst", "target") == 0,
        33);
  fd = open("/pm/vf/dst", O_RDONLY);
  CHECK(fd >= 0 && rename("/pm/vf/src", "/pm/vf/dst") < 0 && close(fd) == 0 &&
          rename("/pm/vf/src", "/pm/vf/dst") == 0 &&
          open("/pm/vf/src", O_RDONLY) < 0 &&
          checkfile("/pm/vf/dst", "source") == 0 &&
          rename("/pm/vf/dst", "/README") < 0 &&
          rename("/README", "/pm/vf/dst") < 0,
        34);

  fd = open("/pm/vf/app", O_CREATE | O_RDWR | O_APPEND);
  CHECK(fd >= 0, 35);
  for (i = 0; i < 3; i++) {
    pid = fork();
    CHECK(pid >= 0, 36);
    if (pid == 0) {
      char c = 'A' + i;
      for (j = 0; j < 20; j++)
        if (write(fd, &c, 1) != 1)
          exit(1);
      exit(0);
    }
  }
  for (i = 0; i < 3; i++)
    CHECK(wait(&status) > 0 && status == 0, 37);
  CHECK(lseek(fd, 0, SEEK_SET) == 0 && write(fd, buf, sizeof(buf)) < 0 &&
          lseek(fd, 0, SEEK_CUR) == 0 && read(fd, buf, 60) == 60 &&
          read(fd, buf + 60, 1) == 0,
        38);
  memset(count, 0, sizeof(count));
  for (i = 0; i < 60; i++) {
    CHECK(buf[i] >= 'A' && buf[i] <= 'C', 39);
    count[buf[i] - 'A']++;
  }
  CHECK(count[0] == 20 && count[1] == 20 && count[2] == 20 && fsync(fd) == 0 &&
          close(fd) == 0,
        40);

  // Independently opened appenders overlap in time; each byte must appear
  // exactly once even though each struct file has its own offset.
  CHECK(writefile("/pm/vf/multi", "") == 0 && pipe(ready) == 0 && pipe(go) == 0,
        43);
  for (i = 0; i < 3; i++) {
    pid = fork();
    CHECK(pid >= 0, 44);
    if (pid == 0) {
      char c = 'A' + i;
      close(ready[0]);
      close(go[1]);
      fd = open("/pm/vf/multi", O_WRONLY | O_APPEND);
      if (fd < 0 || write(ready[1], "r", 1) != 1 || read(go[0], buf, 1) != 1)
        exit(1);
      for (j = 0; j < 20; j++)
        if (write(fd, &c, 1) != 1)
          exit(1);
      if (close(fd) != 0)
        exit(1);
      exit(0);
    }
  }
  close(ready[1]);
  close(go[0]);
  for (i = 0; i < 3; i++)
    CHECK(read(ready[0], buf, 1) == 1, 45);
  for (i = 0; i < 3; i++)
    CHECK(write(go[1], "g", 1) == 1, 46);
  for (i = 0; i < 3; i++)
    CHECK(wait(&status) > 0 && status == 0, 47);
  close(ready[0]);
  close(go[1]);
  fd = open("/pm/vf/multi", O_RDONLY);
  CHECK(fd >= 0 && read(fd, buf, 60) == 60 && read(fd, buf + 60, 1) == 0 &&
          close(fd) == 0,
        48);
  memset(count, 0, sizeof(count));
  for (i = 0; i < 60; i++) {
    CHECK(buf[i] >= 'A' && buf[i] <= 'C', 49);
    count[buf[i] - 'A']++;
  }
  CHECK(count[0] == 20 && count[1] == 20 && count[2] == 20, 55);

  // More than eight independent descriptors to one inode reuse one backend
  // slot. A failed fd allocation must not leak that slot or the inode handle.
  for (i = 0; i < 16; i++) {
    fds[i] = open("/pm/vf/multi", O_RDONLY);
    if (fds[i] < 0)
      break;
  }
  CHECK(i > 8 && i < 16, 59);
  for (j = 0; j < i; j++)
    CHECK(close(fds[j]) == 0, 62);
  CHECK(unlink("/pm/vf/multi") == 0, 63);
}

// Exhaust the append-only metadata allocator in a disposable EMUL image.
// A failed rename must roll the in-memory tree back to the last committed one.
static void
capacity(const char *mode)
{
  const char *src = "/pm/c/a", *dst = "/pm/c/b", *tmp;
  int i;
  CHECK(mkdir("/pm/c") == 0 && writefile(src, "capacity-data") == 0 &&
          writefile("/pm/c/g", "guard") == 0,
        60);
  for (i = 0; i < 2000; i++) {
    if (rename(src, dst) != 0)
      break;
    tmp = src;
    src = dst;
    dst = tmp;
  }
  CHECK(i > 0 && i < 2000 && checkfile(src, "capacity-data") == 0 &&
          open(dst, O_RDONLY) < 0 && checkfile("/pm/c/g", "guard") == 0 &&
          rename(src, dst) < 0 && checkfile(src, "capacity-data") == 0,
        61);
  printf("VFS_CAPACITY_PASS successful_renames=%d\n", i);
}

// A separate fresh MAP/NVDIMM image is used for each fault test. One rename
// moves a directory and its two descendants in a single metadata checkpoint.
static void
prepare(const char *mode)
{
  CHECK(mkdir("/pm/r") == 0 && mkdir("/pm/r/old") == 0 &&
          mkdir("/pm/r/old/sub") == 0 &&
          writefile("/pm/r/old/sub/item", "rename-data") == 0 &&
          writefile("/pm/r/guard", "guard") == 0,
        50);
}

static void
verify(const char *mode, int is_new)
{
  const char *have = is_new ? "/pm/r/new/sub/item" : "/pm/r/old/sub/item";
  const char *missing = is_new ? "/pm/r/old/sub/item" : "/pm/r/new/sub/item";
  CHECK(checkfile(have, "rename-data") == 0 && open(missing, O_RDONLY) < 0 &&
          checkfile("/pm/r/guard", "guard") == 0,
        51);
}

int
main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "basic";
  if (strcmp(mode, "basic") == 0)
    basic(mode);
  else if (strcmp(mode, "capacity") == 0)
    capacity(mode);
  else if (strcmp(mode, "prepare") == 0)
    prepare(mode);
  else if (strcmp(mode, "old") == 0)
    verify(mode, 0);
  else if (strcmp(mode, "new") == 0)
    verify(mode, 1);
  else if (strcmp(mode, "move") == 0) {
    CHECK(rename("/pm/r/old", "/pm/r/new") == 0, 52);
    verify(mode, 1);
  } else if (strcmp(mode, "fault") == 0 && argc == 4) {
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 53);
    rename("/pm/r/old", "/pm/r/new");
    fail(mode, 54); // fault-enabled kernel must stop at the requested boundary
  } else
    fail(mode, 99);
  printf("VFS_TEST_PASS mode=%s\n", mode);
  exit(0);
}
