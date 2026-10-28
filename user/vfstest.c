// Namespace, descriptor and COW rename tests. Run only on disposable media.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/vfs.h"
#include "kernel/pmstats.h"
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
  struct stat st, st2;
  struct vfs_dirent ent;
  struct dirent de;

  // Native same-directory, non-replacing regular-file rename shares one log
  // transaction and preserves open descriptors. Unsupported cases fail closed.
  CHECK(mkdir("/vn") == 0 && mkdir("/vn/sub") == 0 &&
          writefile("/vn/a", "native") == 0 &&
          writefile("/vn/occupied", "other") == 0,
        101);
  fd = open("/vn/a", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && rename("/vn/a", "/vn/b") == 0 &&
          open("/vn/a", O_RDONLY) < 0 && checkfile("/vn/b", "native") == 0 &&
          fstat(fd, &st2) == 0 && st.ino == st2.ino && read(fd, buf, 6) == 6 &&
          memcmp(buf, "native", 6) == 0 && close(fd) == 0,
        102);
  CHECK(rename("/vn/b", "/vn/b") == 0 && link("/vn/b", "/vn/alias") == 0 &&
          rename("/vn/b", "/vn/alias") == 0 &&
          checkfile("/vn/b", "native") == 0 &&
          checkfile("/vn/alias", "native") == 0 &&
          rename("/vn/b", "/vn/occupied") < 0 &&
          rename("/vn/b", "/vn/sub/moved") < 0 &&
          rename("/vn/sub", "/vn/dir2") < 0 && rename("/vn/b/", "/vn/c") < 0 &&
          checkfile("/vn/occupied", "other") == 0,
        103);

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

  // A hard link names the same inode; unlinking a nonfinal name is safe even
  // while a descriptor is open, and rename between aliases is a no-op.
  CHECK(mkdir("/pm/vf/links") == 0 &&
          link("/pm/vf/links", "/pm/vf/dirlink") < 0 &&
          link("README", "/pm/vf/native-link") < 0,
        69);
  fd = open("/pm/vf/links/a", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, "A", 1) == 1 &&
          link("/pm/vf/links/a", "/pm/vf/links/b") == 0 &&
          link("/pm/vf/links/a", "/pm/vf/links/b") < 0 && fstat(fd, &st) == 0 &&
          st.nlink == 2 && fsync(fd) == 0,
        70);
  fd2 = open("/pm/vf/links/b", O_RDONLY);
  CHECK(fd2 >= 0 && fstat(fd2, &st2) == 0 && st2.nlink == 2 &&
          st.ino == st2.ino && read(fd2, buf, 1) == 1 && buf[0] == 'A' &&
          close(fd2) == 0 && rename("/pm/vf/links/a", "/pm/vf/links/b") == 0 &&
          unlink("/pm/vf/links/a") == 0 && fstat(fd, &st) == 0 &&
          st.nlink == 1 && unlink("/pm/vf/links/b") < 0 && close(fd) == 0 &&
          checkfile("/pm/vf/links/b", "A") == 0 &&
          unlink("/pm/vf/links/b") == 0,
        71);

  // Replacing a linked target removes only its destination name; an open fd
  // and the remaining alias continue to reference its original inode.
  CHECK(writefile("/pm/vf/links/t", "old") == 0 &&
          link("/pm/vf/links/t", "/pm/vf/links/u") == 0 &&
          writefile("/pm/vf/links/s", "new") == 0,
        72);
  fd = open("/pm/vf/links/t", O_RDWR);
  CHECK(fd >= 0 && write(fd, "O", 1) == 1 &&
          rename("/pm/vf/links/s", "/pm/vf/links/t") == 0 &&
          fstat(fd, &st) == 0 && st.nlink == 1 && fsync(fd) == 0 &&
          lseek(fd, 0, SEEK_SET) == 0 && read(fd, buf, 3) == 3 &&
          memcmp(buf, "Old", 3) == 0 && close(fd) == 0 &&
          checkfile("/pm/vf/links/u", "Old") == 0 &&
          checkfile("/pm/vf/links/t", "new") == 0,
        73);

  // Dedicated streams cache a working inode. A link created while one is
  // dirty must not be undone when that stale work inode is later committed.
  CHECK(pmfs_create("/vf/links/h") == 0 &&
          (fd = pmfs_fopen("/vf/links/h")) >= 0 &&
          pmfs_fwrite(fd, "stream", 6) == 6 &&
          link("/pm/vf/links/h", "/pm/vf/links/i") == 0 &&
          pmfs_fsync(fd) == 0 && pmfs_fclose(fd) == 0,
        87);
  fd = open("/pm/vf/links/h", O_RDONLY);
  fd2 = open("/pm/vf/links/i", O_RDONLY);
  CHECK(fd >= 0 && fd2 >= 0 && fstat(fd, &st) == 0 && fstat(fd2, &st2) == 0 &&
          st.nlink == 2 && st2.nlink == 2 && st.ino == st2.ino &&
          close(fd) == 0 && close(fd2) == 0 &&
          checkfile("/pm/vf/links/i", "stream") == 0,
        88);

  // Multiple harts add/open/remove different aliases to the same inode. The
  // permanent name keeps it linked, while every link-count transition must
  // remain serializable under the global PMFS lock.
  CHECK(writefile("/pm/vf/links/p", "parallel") == 0, 95);
  for (i = 0; i < 3; i++) {
    pid = fork();
    CHECK(pid >= 0, 96);
    if (pid == 0) {
      const char *alias = i == 0   ? "/pm/vf/links/p0"
                          : i == 1 ? "/pm/vf/links/p1"
                                   : "/pm/vf/links/p2";
      for (j = 0; j < 20; j++) {
        if (link("/pm/vf/links/p", alias) != 0 ||
            checkfile(alias, "parallel") != 0 || unlink(alias) != 0)
          exit(1);
      }
      exit(0);
    }
  }
  for (i = 0; i < 3; i++)
    CHECK(wait(&status) > 0 && status == 0, 97);
  fd = open("/pm/vf/links/p", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.nlink == 1 && close(fd) == 0 &&
          unlink("/pm/vf/links/p") == 0,
        98);

  // Race global sync against the last user close; sync must not extend the
  // file object's lifetime with an invisible reference that delays close.
  for (i = 0; i < 12; i++) {
    fd = open("/pm/vf/sync", O_CREATE | O_RDWR | O_TRUNC);
    CHECK(fd >= 0 && write(fd, "sync", 4) == 4 && pipe(ready) == 0 &&
            pipe(go) == 0,
          64);
    pid = fork();
    CHECK(pid >= 0, 65);
    if (pid == 0) {
      close(ready[0]);
      close(go[1]);
      if (close(fd) != 0 || write(ready[1], "r", 1) != 1 ||
          read(go[0], buf, 1) != 1 || sync() != 0)
        exit(1);
      exit(0);
    }
    close(ready[1]);
    close(go[0]);
    CHECK(read(ready[0], buf, 1) == 1 && write(go[1], "g", 1) == 1 &&
            close(fd) == 0 && wait(&status) == pid && status == 0 &&
            checkfile("/pm/vf/sync", "sync") == 0,
          66);
    close(ready[0]);
    close(go[1]);
  }
}

// More metadata-only updates than the entire EMUL device has pages must work
// without exhausting its append frontier. This is not full-device KV GC.
static void
capacity(const char *mode)
{
  const char *src = "/pm/c/a", *dst = "/pm/c/b", *tmp;
  struct pmfs_metrics before, after;
  int i, fd;
  CHECK(mkdir("/pm/c") == 0 && writefile(src, "capacity-data") == 0 &&
          writefile("/pm/c/g", "guard") == 0 &&
          (fd = open("/pm/c/dirty", O_CREATE | O_RDWR)) >= 0 &&
          write(fd, "dirty", 5) == 5 &&
          pmfs_metrics(&before, sizeof(before)) == 0,
        60);
  for (i = 0; i < 5000; i++) {
    CHECK(rename(src, dst) == 0, 61);
    tmp = src;
    src = dst;
    dst = tmp;
  }
  CHECK(pmfs_metrics(&after, sizeof(after)) == 0 &&
          after.gc_reused_pages - before.gc_reused_pages >= 4998 &&
          after.gc_failed_scans == before.gc_failed_scans &&
          after.gc_trace_runs - before.gc_trace_runs <=
            2 * before.kv_runs + 2 &&
          after.fs_tail_pages <= before.fs_tail_pages + 2 &&
          checkfile(src, "capacity-data") == 0 && open(dst, O_RDONLY) < 0 &&
          checkfile("/pm/c/g", "guard") == 0 && sync() == 0 && close(fd) == 0 &&
          checkfile("/pm/c/dirty", "dirty") == 0,
        67);
  printf("VFS_CAPACITY_PASS successful_renames=%d reused=%lu tail_before=%lu "
         "tail_after=%lu scans=%lu traced_runs=%lu read_bytes=%lu\n",
         i, after.gc_reused_pages - before.gc_reused_pages,
         before.fs_tail_pages, after.fs_tail_pages,
         after.gc_scans - before.gc_scans,
         after.gc_trace_runs - before.gc_trace_runs,
         after.dev_read_bytes - before.dev_read_bytes);
}

// A separate fresh MAP/NVDIMM image is used for each fault test. One rename
// moves a directory and its two descendants in a single metadata checkpoint.
static void
prepare(const char *mode, int replace)
{
  CHECK(mkdir("/pm/r") == 0 && mkdir("/pm/r/old") == 0 &&
          mkdir("/pm/r/old/sub") == 0 &&
          writefile("/pm/r/old/sub/item", "rename-data") == 0 &&
          writefile("/pm/r/guard", "guard") == 0 &&
          (!replace || mkdir("/pm/r/new") == 0),
        50);
}

static void
verify(const char *mode, int is_new, int replace)
{
  const char *have = is_new ? "/pm/r/new/sub/item" : "/pm/r/old/sub/item";
  const char *missing = is_new ? "/pm/r/old/sub/item" : "/pm/r/new/sub/item";
  int fd;
  CHECK(checkfile(have, "rename-data") == 0 && open(missing, O_RDONLY) < 0 &&
          checkfile("/pm/r/guard", "guard") == 0,
        51);
  if (!is_new) {
    fd = open("/pm/r/new", O_RDONLY);
    CHECK(replace ? (fd >= 0 && close(fd) == 0) : (fd < 0), 68);
  }
}

static void
verify_link_stream(const char *mode, int is_new)
{
  struct stat a, b;
  const char *data = is_new ? "new" : "old";
  int fd = open("/pm/s/a", O_RDONLY), other = open("/pm/s/b", O_RDONLY);
  CHECK(fd >= 0 && other >= 0 && fstat(fd, &a) == 0 && fstat(other, &b) == 0 &&
          a.ino == b.ino && a.nlink == 2 && b.nlink == 2 && close(fd) == 0 &&
          close(other) == 0 && checkfile("/pm/s/a", data) == 0 &&
          checkfile("/pm/s/b", data) == 0,
        89);
}

static void
verify_gc_slots(const char *mode, int at_b)
{
  const char *have = at_b ? "/pm/d/b" : "/pm/d/a";
  const char *missing = at_b ? "/pm/d/a" : "/pm/d/b";
  CHECK(checkfile(have, "dual-root") == 0 && open(missing, O_RDONLY) < 0 &&
          checkfile("/pm/d/g", "guard") == 0,
        82);
}

static void
verify_links(const char *mode, int linked)
{
  struct stat a, b;
  int fd = open("/pm/l/a", O_RDONLY), other;
  CHECK(fd >= 0 && fstat(fd, &a) == 0 && a.nlink == (linked ? 2 : 1) &&
          read(fd, buf, 9) == 9 && memcmp(buf, "link-data", 9) == 0 &&
          close(fd) == 0 && checkfile("/pm/l/guard", "guard") == 0,
        74);
  other = open("/pm/l/b", O_RDONLY);
  if (linked) {
    CHECK(other >= 0 && fstat(other, &b) == 0 && b.ino == a.ino &&
            b.nlink == 2 && read(other, buf, 9) == 9 &&
            memcmp(buf, "link-data", 9) == 0 && close(other) == 0,
          75);
  } else {
    CHECK(other < 0, 76);
  }
}

int
main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "basic";
  if (strcmp(mode, "basic") == 0)
    basic(mode);
  else if (strcmp(mode, "capacity") == 0)
    capacity(mode);
  else if (strcmp(mode, "native-init") == 0) {
    CHECK(mkdir("/rn") == 0 && writefile("/rn/a", "rename-data") == 0 &&
            writefile("/rn/guard", "guard") == 0,
          104);
  } else if (strcmp(mode, "native-old") == 0 ||
             strcmp(mode, "native-new") == 0) {
    int is_new = strcmp(mode, "native-new") == 0;
    CHECK(checkfile(is_new ? "/rn/b" : "/rn/a", "rename-data") == 0 &&
            open(is_new ? "/rn/a" : "/rn/b", O_RDONLY) < 0 &&
            checkfile("/rn/guard", "guard") == 0,
          105);
  } else if (strcmp(mode, "native-move") == 0) {
    CHECK(rename("/rn/a", "/rn/b") == 0, 106);
  } else if (strcmp(mode, "native-fault") == 0 && argc == 3) {
    CHECK(xv6_log_fault(atoi(argv[2])) == 0, 107);
    rename("/rn/a", "/rn/b");
    fail(mode, 108);
  } else if (strcmp(mode, "prepare") == 0 ||
             strcmp(mode, "prepare-replace") == 0)
    prepare(mode, strcmp(mode, "prepare-replace") == 0);
  else if (strcmp(mode, "old") == 0 || strcmp(mode, "old-replace") == 0)
    verify(mode, 0, strcmp(mode, "old-replace") == 0);
  else if (strcmp(mode, "new") == 0)
    verify(mode, 1, 0);
  else if (strcmp(mode, "move") == 0) {
    CHECK(rename("/pm/r/old", "/pm/r/new") == 0, 52);
    verify(mode, 1, 0);
  } else if (strcmp(mode, "stream-prepare") == 0) {
    CHECK(mkdir("/pm/s") == 0 && writefile("/pm/s/a", "old") == 0 &&
            link("/pm/s/a", "/pm/s/b") == 0,
          90);
    verify_link_stream(mode, 0);
  } else if (strcmp(mode, "stream-old") == 0) {
    verify_link_stream(mode, 0);
  } else if (strcmp(mode, "stream-new") == 0) {
    verify_link_stream(mode, 1);
  } else if (strcmp(mode, "stream-update") == 0 ||
             strcmp(mode, "stream-dirty") == 0 ||
             (strcmp(mode, "stream-fault") == 0 && argc == 4)) {
    int fd = pmfs_fopen("/s/a");
    CHECK(fd >= 0 && pmfs_fwrite(fd, "new", 3) == 3, 91);
    if (strcmp(mode, "stream-dirty") == 0) {
      printf("VFS_LINK_DIRTY_READY\n");
      for (;;)
        ;
    }
    if (strcmp(mode, "stream-fault") == 0)
      CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 92);
    CHECK(pmfs_fsync(fd) == 0 && pmfs_fclose(fd) == 0, 93);
    if (strcmp(mode, "stream-fault") == 0)
      fail(mode, 94);
    verify_link_stream(mode, 1);
  } else if (strcmp(mode, "gc-prepare") == 0) {
    CHECK(mkdir("/pm/d") == 0 && writefile("/pm/d/a", "dual-root") == 0 &&
            writefile("/pm/d/g", "guard") == 0 &&
            rename("/pm/d/a", "/pm/d/b") == 0,
          83);
    verify_gc_slots(mode, 1);
  } else if (strcmp(mode, "gc-a") == 0) {
    verify_gc_slots(mode, 0);
  } else if (strcmp(mode, "gc-b") == 0) {
    verify_gc_slots(mode, 1);
  } else if (strcmp(mode, "gc-move") == 0) {
    CHECK(rename("/pm/d/b", "/pm/d/a") == 0, 84);
    verify_gc_slots(mode, 0);
  } else if (strcmp(mode, "gc-fault") == 0 && argc == 4) {
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 85);
    rename("/pm/d/b", "/pm/d/a");
    fail(mode, 86);
  } else if (strcmp(mode, "link-prepare") == 0) {
    CHECK(mkdir("/pm/l") == 0 && writefile("/pm/l/a", "link-data") == 0 &&
            writefile("/pm/l/guard", "guard") == 0,
          77);
    verify_links(mode, 0);
  } else if (strcmp(mode, "link-one") == 0) {
    verify_links(mode, 0);
  } else if (strcmp(mode, "link-two") == 0) {
    verify_links(mode, 1);
  } else if (strcmp(mode, "link-add") == 0) {
    CHECK(link("/pm/l/a", "/pm/l/b") == 0, 78);
    verify_links(mode, 1);
  } else if (strcmp(mode, "link-drop") == 0) {
    CHECK(unlink("/pm/l/b") == 0, 79);
    verify_links(mode, 0);
  } else if ((strcmp(mode, "fault-link-add") == 0 ||
              strcmp(mode, "fault-link-drop") == 0) &&
             argc == 4) {
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 80);
    if (strcmp(mode, "fault-link-add") == 0)
      link("/pm/l/a", "/pm/l/b");
    else
      unlink("/pm/l/b");
    fail(mode, 81);
  } else if (strcmp(mode, "fault") == 0 && argc == 4) {
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 53);
    rename("/pm/r/old", "/pm/r/new");
    fail(mode, 54); // fault-enabled kernel must stop at the requested boundary
  } else
    fail(mode, 99);
  printf("VFS_TEST_PASS mode=%s\n", mode);
  exit(0);
}
