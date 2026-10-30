// Native block-filesystem regression and recovery workload. All persistent
// tests must run on disposable disk images. This is not the direct PMEM VFS.
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PAGE_BYTES  4096
#define LAST_BYTE   (MAXFILE * BSIZE - 1)
#define DIRECT_END  (NDIRECT * BSIZE - 1)
#define INDIRECT_AT (NDIRECT * BSIZE + 4 * BSIZE + 17)
#define PATCH_AT    (6 * BSIZE + 123)

static char buf[3 * BSIZE];

static void __attribute__((noreturn))
fail(int code)
{
  printf("NATIVE_IO_FAIL code=%d\n", code);
  exit(1);
}

#define CHECK(cond, code)                                                      \
  do {                                                                         \
    if (!(cond))                                                               \
      fail(code);                                                              \
  } while (0)

static char
expected(uint at, int patched)
{
  if (at < 4)
    return "HEAD"[at];
  if (at == DIRECT_END)
    return 'D';
  if (at >= INDIRECT_AT && at < INDIRECT_AT + 3)
    return "IND"[at - INDIRECT_AT];
  if (at == LAST_BYTE)
    return 'Z';
  if (patched && at >= PATCH_AT && at < PATCH_AT + 3)
    return "NEW"[at - PATCH_AT];
  return 0;
}

static void
seed(void)
{
  int fd = open("ni-sparse", O_CREATE | O_TRUNC | O_RDWR);
  CHECK(fd >= 0, 1);
  CHECK(write(fd, "HEAD", 4) == 4, 2);
  CHECK(lseek(fd, DIRECT_END, SEEK_SET) == DIRECT_END && write(fd, "D", 1) == 1,
        3);
  CHECK(lseek(fd, INDIRECT_AT, SEEK_SET) == INDIRECT_AT &&
          write(fd, "IND", 3) == 3,
        4);
  CHECK(lseek(fd, LAST_BYTE, SEEK_SET) == LAST_BYTE && write(fd, "Z", 1) == 1,
        5);
  CHECK(fsync(fd) == 0 && close(fd) == 0, 6);
  printf("NATIVE_IO_PASS mode=seed\n");
}

static void
verify(int patched)
{
  struct stat st;
  int fd = open("ni-sparse", O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.size == LAST_BYTE + 1, 10);
  for (uint off = 0; off < LAST_BYTE + 1; off += BSIZE) {
    CHECK(read(fd, buf, BSIZE) == BSIZE, 11);
    for (uint i = 0; i < BSIZE; i++)
      if (buf[i] != expected(off + i, patched))
        fail(12);
  }
  CHECK(read(fd, buf, 1) == 0 && close(fd) == 0, 13);
  printf("NATIVE_IO_PASS mode=%s\n", patched ? "new" : "old");
}

static void
fault(int stage)
{
  int fd = open("ni-sparse", O_RDWR);
  CHECK(fd >= 0 && lseek(fd, PATCH_AT, SEEK_SET) == PATCH_AT, 20);
  CHECK(stage >= 1 && stage <= 5 && xv6_log_fault(stage) == 0, 21);
  if (write(fd, "NEW", 3) != 3)
    fail(22);
  fail(23); // a fault-enabled kernel must stop at the selected boundary
}

static void
io_semantics(void)
{
  struct stat st;
  int fd = open("ni-io", O_CREATE | O_TRUNC | O_RDWR);
  CHECK(fd >= 0, 30);
  uint64 old = (uint64)sbrk(0);
  int grow = (PAGE_BYTES - old % PAGE_BYTES) % PAGE_BYTES + PAGE_BYTES;
  CHECK(sbrk(grow) == (char *)old, 31);
  char *edge = (char *)(old + grow - 64);
  memset(edge, 'w', 64);

  // Crossing into an unmapped page must return the valid prefix, update the
  // shared offset by exactly that count and leave the rest unmodified.
  CHECK(write(fd, edge, 128) == 64 && fstat(fd, &st) == 0 && st.size == 64 &&
          lseek(fd, 0, SEEK_CUR) == 64,
        32);
  CHECK(lseek(fd, 100, SEEK_SET) == 100 && write(fd, edge + 64, 1) < 0 &&
          fstat(fd, &st) == 0 && st.size == 64 && lseek(fd, 0, SEEK_CUR) == 100,
        33);
  CHECK(lseek(fd, 0, SEEK_SET) == 0, 34);
  memset(buf, 'r', 128);
  CHECK(write(fd, buf, 128) == 128 && lseek(fd, 0, SEEK_SET) == 0, 35);
  memset(edge, 0, 64);
  CHECK(read(fd, edge, 128) == 64 && lseek(fd, 0, SEEK_CUR) == 64, 36);
  for (int i = 0; i < 64; i++)
    CHECK(edge[i] == 'r', 37);
  CHECK(read(fd, buf, 64) == 64, 38);

  CHECK(lseek(fd, LAST_BYTE - 3, SEEK_SET) == LAST_BYTE - 3 &&
          write(fd, "abcdefgh", 8) == 4 && fstat(fd, &st) == 0 &&
          st.size == LAST_BYTE + 1 && lseek(fd, 0, SEEK_CUR) == LAST_BYTE + 1 &&
          write(fd, "x", 1) < 0 && fstat(fd, &st) == 0 &&
          st.size == LAST_BYTE + 1,
        39);
  CHECK(lseek(fd, LAST_BYTE - 3, SEEK_SET) == LAST_BYTE - 3 &&
          read(fd, buf, 4) == 4 && memcmp(buf, "abcd", 4) == 0 &&
          fsync(fd) == 0 && close(fd) == 0,
        40);
  CHECK(sbrk(-grow) != SBRK_ERROR, 41);
  printf("NATIVE_IO_PASS mode=semantics\n");
}

static void
fill_name(char name[4], int i)
{
  name[0] = 'q';
  name[1] = '0' + i / 10;
  name[2] = '0' + i % 10;
  name[3] = 0;
}

static void
nospace(void)
{
  char name[4];
  struct stat st;
  int fd, full = 0;
  memset(buf, 'v', sizeof(buf));

  // Pre-create every inode and retain one data block, so the test can make
  // exactly one block available after reaching a real ENOSPC condition.
  int reserve = open("ni-reserve", O_CREATE | O_TRUNC | O_RDWR);
  int victim = open("ni-victim", O_CREATE | O_TRUNC | O_RDWR);
  CHECK(reserve >= 0 && victim >= 0 && write(reserve, buf, BSIZE) == BSIZE &&
          close(reserve) == 0,
        50);
  for (int i = 0; i < 16; i++) {
    fill_name(name, i);
    fd = open(name, O_CREATE | O_TRUNC | O_RDWR);
    CHECK(fd >= 0 && close(fd) == 0, 51);
  }
  for (int i = 0; i < 16 && !full; i++) {
    fill_name(name, i);
    fd = open(name, O_RDWR);
    CHECK(fd >= 0, 52);
    int written = 0;
    for (;;) {
      int r = write(fd, buf, sizeof(buf));
      if (r > 0)
        written += r;
      CHECK(fstat(fd, &st) == 0 && st.size == (uint)written &&
              lseek(fd, 0, SEEK_CUR) == written,
            53);
      if (r != sizeof(buf)) {
        if (written != MAXFILE * BSIZE)
          full = 1;
        break;
      }
    }
    CHECK(close(fd) == 0, 54);
  }
  CHECK(full, 55);
  CHECK(lseek(victim, INDIRECT_AT, SEEK_SET) == INDIRECT_AT &&
          write(victim, "x", 1) < 0 && fstat(victim, &st) == 0 &&
          st.size == 0 && lseek(victim, 0, SEEK_CUR) == INDIRECT_AT,
        56);
  CHECK(unlink("ni-reserve") == 0 && lseek(victim, 0, SEEK_SET) == 0, 57);
  CHECK(write(victim, buf, 2 * BSIZE) == BSIZE && fstat(victim, &st) == 0 &&
          st.size == BSIZE && lseek(victim, 0, SEEK_CUR) == BSIZE &&
          fsync(victim) == 0,
        58);
  CHECK(lseek(victim, 0, SEEK_SET) == 0 && read(victim, buf, BSIZE) == BSIZE,
        59);
  for (int i = 0; i < BSIZE; i++)
    CHECK(buf[i] == 'v', 60);
  CHECK(close(victim) == 0, 61);
  printf("NATIVE_IO_PASS mode=nospace\n");
}

// Freeze a real log commit, start both fd-specific and global sync, and
// require both to be sleeping behind it. Either leave QEMU for the host to
// kill, or release the cut and prove both calls eventually acknowledge.
static void
sync_wait(int stage, int release)
{
  int ready[2], done[2], pid, children[3], status;
  char signal;
  int fd = open("ni-sparse", O_RDWR);
  CHECK(fd >= 0 && stage >= 1 && stage <= 5 && pipe(ready) == 0 &&
          pipe(done) == 0,
        70);
  children[0] = fork();
  CHECK(children[0] >= 0, 71);
  if (children[0] == 0) {
    close(ready[0]);
    close(done[0]);
    close(done[1]);
    if (lseek(fd, PATCH_AT, SEEK_SET) != PATCH_AT)
      exit(1);
    int armed = 0;
    for (int i = 0; i < 30 && !armed; i++) {
      if (xv6_log_fault(stage) == 0)
        armed = 1;
      else
        pause(1);
    }
    signal = armed ? 'R' : 'F';
    if (write(ready[1], &signal, 1) != 1 || !armed || write(fd, "NEW", 3) != 3)
      exit(1);
    exit(0);
  }
  close(ready[1]);
  CHECK(read(ready[0], &signal, 1) == 1 && signal == 'R' &&
          close(ready[0]) == 0,
        72);
  int reached = 0;
  for (int i = 0; i < 200 && !reached; i++) {
    reached = xv6_log_fault(6) == stage;
    if (!reached)
      pause(1);
  }
  CHECK(reached, 73);
  for (int i = 0; i < 2; i++) {
    children[i + 1] = fork();
    CHECK(children[i + 1] >= 0, 74);
    if (children[i + 1] == 0) {
      close(done[0]);
      int rc = i == 0 ? fsync(fd) : sync();
      signal = i == 0 ? 'F' : 'G';
      if (rc != 0 || write(done[1], &signal, 1) != 1)
        exit(1);
      exit(0);
    }
  }
  close(done[1]);
  int waiting = 0;
  for (int i = 0; i < 200 && !waiting; i++) {
    waiting = xv6_log_fault(7) >= 2;
    if (!waiting)
      pause(1);
  }
  CHECK(waiting, 75);
  printf("NATIVE_SYNC_WAIT stage=%d waiters=2\n", stage);
  if (!release)
    for (;;)
      pause(10);
  CHECK(xv6_log_fault(8) == 0, 76);
  int seen = 0;
  for (int i = 0; i < 2; i++) {
    CHECK(read(done[0], &signal, 1) == 1, 77);
    seen |= signal == 'F' ? 1 : signal == 'G' ? 2 : 0;
  }
  CHECK(seen == 3 && close(done[0]) == 0, 78);
  for (int i = 0; i < 3; i++) {
    pid = wait(&status);
    CHECK(pid > 0 && status == 0, 79);
  }
  CHECK(close(fd) == 0, 80);
  printf("NATIVE_IO_PASS mode=sync-release\n");
}

int
main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "seed") == 0)
    seed();
  else if (argc == 2 && strcmp(argv[1], "old") == 0)
    verify(0);
  else if (argc == 2 && strcmp(argv[1], "new") == 0)
    verify(1);
  else if (argc == 3 && strcmp(argv[1], "fault") == 0)
    fault(atoi(argv[2]));
  else if (argc == 3 && strcmp(argv[1], "sync-crash") == 0)
    sync_wait(atoi(argv[2]), 0);
  else if (argc == 3 && strcmp(argv[1], "sync-release") == 0)
    sync_wait(atoi(argv[2]), 1);
  else if (argc == 2 && strcmp(argv[1], "semantics") == 0)
    io_semantics();
  else if (argc == 2 && strcmp(argv[1], "nospace") == 0)
    nospace();
  else
    fail(99);
  exit(0);
}
