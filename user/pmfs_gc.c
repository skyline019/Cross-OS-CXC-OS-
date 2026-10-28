// Reclamation/compaction correctness and software-workload regression.
// Run only on disposable PMFS media.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/pmstats.h"
#include "user/user.h"

#define BASE    0x2200000000000000UL
#define PENDING 0x2300000000000001UL
#define NKEYS   600
#define CHECK(c, code)                                                         \
  do {                                                                         \
    if (!(c)) {                                                                \
      printf("PMFS_GC_FAIL mode=%s code=%d\n", mode, code);                    \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static char value[80], got[1024], filedata[1024];

static void
make_value(int index, int generation)
{
  int i;
  for (i = 0; i < sizeof(value); i++)
    value[i] = 'A' + ((index * 7 + i * 3 + generation * 11) % 26);
}

static void
make_file(void)
{
  int i;
  for (i = 0; i < sizeof(filedata); i++)
    filedata[i] = (char)(i * 17 + i % 7);
}

static void
populate(const char *mode)
{
  int i, fd;
  CHECK(mkdir("/pm/g") == 0, 1);
  make_file();
  fd = open("/pm/g/f", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, filedata, sizeof(filedata)) == sizeof(filedata) &&
          fsync(fd) == 0 && lseek(fd, 0, SEEK_SET) == 0 &&
          write(fd, filedata, 1) == 1 && close(fd) == 0 &&
          link("/pm/g/f", "/pm/g/alias") == 0,
        2);
  // A later, deleted chunk is kept only as the internal-ID high-water marker.
  fd = open("/pm/g/dead", O_CREATE | O_RDWR);
  CHECK(fd >= 0 && write(fd, "discarded", 9) == 9 && fsync(fd) == 0 &&
          lseek(fd, 0, SEEK_SET) == 0 && write(fd, "discarded2", 10) == 10 &&
          close(fd) == 0 && unlink("/pm/g/dead") == 0,
        3);
  for (i = 0; i < NKEYS; i++) {
    make_value(i, 0);
    CHECK(pmfs_put(BASE + i, value, sizeof(value)) == 0, 4);
    if (i % 8 == 7)
      CHECK(pmfs_seal() == 0, 5);
  }
  for (i = 0; i < 80; i++) {
    make_value(i, 1);
    CHECK(pmfs_put(BASE + i, value, sizeof(value)) == 0, 6);
    if (i % 8 == 7)
      CHECK(pmfs_seal() == 0, 7);
  }
  CHECK(pmfs_put(PENDING, "pending", 7) == 0 && pmfs_seal() == 0, 8);
}

static void
verify(const char *mode)
{
  struct stat a, b;
  struct pmfs_metrics m;
  uint len;
  int i, fd, alias;
  make_file();
  fd = open("/pm/g/f", O_RDONLY);
  alias = open("/pm/g/alias", O_RDONLY);
  CHECK(fd >= 0 && alias >= 0 && fstat(fd, &a) == 0 && fstat(alias, &b) == 0 &&
          a.ino == b.ino && a.nlink == 2 && b.nlink == 2 &&
          read(fd, got, sizeof(got)) == sizeof(got) &&
          memcmp(got, filedata, sizeof(got)) == 0 && close(fd) == 0 &&
          close(alias) == 0 && open("/pm/g/dead", O_RDONLY) < 0,
        9);
  for (i = 0; i < NKEYS; i++) {
    make_value(i, i < 80);
    len = 0;
    CHECK(pmfs_get(BASE + i, got, sizeof(value), &len) == 1 &&
            len == sizeof(value) && memcmp(got, value, sizeof(value)) == 0,
          10);
  }
  len = 0;
  CHECK(pmfs_get(PENDING, got, sizeof(value), &len) == 1 && len == 7 &&
          memcmp(got, "pending", 7) == 0 &&
          pmfs_get(BASE + NKEYS + 9, got, sizeof(value), &len) == 0 &&
          pmfs_metrics(&m, sizeof(m)) == 0,
        11);
  printf("PMFS_GC_VIEW runs=%lu tail=%lu reused=%lu\n", m.kv_runs,
         m.fs_tail_pages, m.gc_reused_pages);
}

static void
compact(const char *mode)
{
  struct pmfs_metrics before, after;
  static struct pmfs_metrics q0, q1, q2, q3;
  uint len;
  make_value(550, 0);
  CHECK(pmfs_metrics(&q0, sizeof(q0)) == 0 &&
          pmfs_get(BASE + 550, got, sizeof(value), &len) == 1 &&
          len == sizeof(value) && memcmp(got, value, sizeof(value)) == 0 &&
          pmfs_metrics(&q1, sizeof(q1)) == 0,
        28);
  CHECK(pmfs_metrics(&before, sizeof(before)) == 0 && pmfs_compact() == 0 &&
          pmfs_metrics(&after, sizeof(after)) == 0 &&
          after.compact_calls == before.compact_calls + 1 &&
          after.kv_runs < before.kv_runs &&
          after.compact_input_records - before.compact_input_records >
            after.compact_output_records - before.compact_output_records &&
          after.compact_output_pages > before.compact_output_pages,
        12);
  printf("PMFS_GC_METRIC runs_before=%lu runs_after=%lu input_records=%lu "
         "output_records=%lu output_pages=%lu tail_before=%lu tail_after=%lu "
         "read_bytes=%lu write_bytes=%lu\n",
         before.kv_runs, after.kv_runs,
         after.compact_input_records - before.compact_input_records,
         after.compact_output_records - before.compact_output_records,
         after.compact_output_pages - before.compact_output_pages,
         before.fs_tail_pages, after.fs_tail_pages,
         after.dev_read_bytes - before.dev_read_bytes,
         after.dev_write_bytes - before.dev_write_bytes);
  CHECK(pmfs_metrics(&q2, sizeof(q2)) == 0 &&
          pmfs_get(BASE + 550, got, sizeof(value), &len) == 1 &&
          len == sizeof(value) && memcmp(got, value, sizeof(value)) == 0 &&
          pmfs_metrics(&q3, sizeof(q3)) == 0 &&
          q3.kv_fallback_run_probes - q2.kv_fallback_run_probes <=
            q1.kv_fallback_run_probes - q0.kv_fallback_run_probes,
        29);
  printf("PMFS_GC_QUERY before_probes=%lu after_probes=%lu "
         "before_pages=%lu after_pages=%lu\n",
         q1.kv_fallback_run_probes - q0.kv_fallback_run_probes,
         q3.kv_fallback_run_probes - q2.kv_fallback_run_probes,
         q1.kv_page_reads - q0.kv_page_reads,
         q3.kv_page_reads - q2.kv_page_reads);
  verify(mode);
}

static void
churn(const char *mode)
{
  struct pmfs_metrics a, b;
  int i;
  // The first namespace commit retires the previous checkpoint. Subsequent
  // metadata updates may then reuse pages from its old run chain.
  CHECK(pmfs_metrics(&a, sizeof(a)) == 0, 16);
  for (i = 0; i < 120; i++)
    CHECK(rename(i % 2 ? "/pm/g/fm" : "/pm/g/f",
                 i % 2 ? "/pm/g/f" : "/pm/g/fm") == 0,
          17);
  CHECK(pmfs_metrics(&b, sizeof(b)) == 0 &&
          b.gc_reused_pages - a.gc_reused_pages >= 119 &&
          b.gc_failed_scans == a.gc_failed_scans,
        18);
  verify(mode);
}

static void
basic(const char *mode)
{
  struct pmfs_metrics a, b;
  uint len;
  int fd;
  populate(mode);
  verify(mode);
  // A pending global memtable is not silently sealed or discarded.
  CHECK(
    pmfs_put(PENDING, "pending", 7) == 0 && pmfs_metrics(&a, sizeof(a)) == 0 &&
      pmfs_compact() == PM_COMPACT_BUSY && pmfs_metrics(&b, sizeof(b)) == 0 &&
      a.dev_write_bytes == b.dev_write_bytes &&
      pmfs_get(PENDING, got, sizeof(value), &len) == 1 && len == 7 &&
      pmfs_seal() == 0,
    13);
  fd = open("/pm/g/f", O_RDONLY);
  CHECK(fd >= 0 && pmfs_compact() == PM_COMPACT_BUSY && close(fd) == 0, 14);
  fd = pmfs_fopen("/g/f");
  CHECK(fd >= 0 && pmfs_compact() == PM_COMPACT_BUSY && pmfs_fclose(fd) == 0,
        15);
  compact(mode);
  churn(mode);
}

static void
newchunk(const char *mode, int create)
{
  int fd;
  if (create) {
    fd = open("/pm/g/after", O_CREATE | O_RDWR);
    CHECK(fd >= 0 && write(fd, "after", 5) == 5 && fsync(fd) == 0 &&
            close(fd) == 0,
          30);
  }
  fd = open("/pm/g/after", O_RDONLY);
  CHECK(fd >= 0 && read(fd, got, 5) == 5 && memcmp(got, "after", 5) == 0 &&
          close(fd) == 0,
        31);
  verify(mode);
}

// Repeatedly fail after writing a new metadata page but before publication.
// Cycling the allocator without replacing either slot probes every reusable
// candidate, then the host oracle tears the active checkpoint.
static void
rollback(const char *mode)
{
  struct pmfs_metrics before, after;
  int i, attempts;
  CHECK(pmfs_metrics(&before, sizeof(before)) == 0 &&
          before.fs_tail_pages < 500,
        25);
  attempts = 2 * before.fs_tail_pages;
  for (i = 0; i < attempts; i++)
    CHECK(pmfs_fault(8, 0) == 0 && rename("/pm/g/f", "/pm/g/fm") < 0, 26);
  CHECK(pmfs_metrics(&after, sizeof(after)) == 0 &&
          after.fs_tail_pages == before.fs_tail_pages &&
          after.gc_reused_pages > before.gc_reused_pages,
        27);
  printf("PMFS_GC_ROLLBACK attempts=%d reused=%lu\n", attempts,
         after.gc_reused_pages - before.gc_reused_pages);
  verify(mode);
}

// Used after the test harness corrupts only the older slot's run chain.
// The active chain is readable, but allocation must refuse to guess which old
// pages are safe and must leave both checkpoint headers untouched.
static void
refuse(const char *mode)
{
  struct pmfs_metrics before, after;
  CHECK(pmfs_metrics(&before, sizeof(before)) == 0 &&
          rename("/pm/g/f", "/pm/g/fm") < 0 &&
          pmfs_metrics(&after, sizeof(after)) == 0 &&
          after.gc_failed_scans == before.gc_failed_scans + 1 &&
          after.dev_write_bytes == before.dev_write_bytes &&
          after.fs_tail_pages == before.fs_tail_pages,
        24);
  verify(mode);
}

// Built against a deliberately tiny disposable EMUL device. Fill the fresh
// append frontier while leaving the memtable empty, then check preflight.
static void
nospace(const char *mode)
{
  struct pmfs_metrics m, before, after;
  uint len;
  int i = 0;
  CHECK(pmfs_metrics(&m, sizeof(m)) == 0 && m.fs_capacity_pages <= 64, 19);
  while (m.fs_tail_pages < m.fs_capacity_pages) {
    make_value(i++, 0);
    CHECK(pmfs_put(BASE, value, sizeof(value)) == 0 && pmfs_seal() == 0 &&
            pmfs_metrics(&m, sizeof(m)) == 0 && i < 200,
          20);
  }
  before = m;
  CHECK(pmfs_compact() == PM_COMPACT_NOSPACE && mkdir("/pm/full") < 0 &&
          open("/pm/full", O_RDONLY) < 0 &&
          pmfs_metrics(&after, sizeof(after)) == 0 &&
          after.dev_write_bytes == before.dev_write_bytes &&
          after.fs_tail_pages == before.fs_tail_pages &&
          pmfs_get(BASE, got, sizeof(value), &len) == 1 &&
          len == sizeof(value) && memcmp(got, value, sizeof(value)) == 0,
        21);
  printf("PMFS_GC_NOSPACE_PASS runs=%d tail=%lu capacity=%lu\n", i,
         after.fs_tail_pages, after.fs_capacity_pages);
}

int
main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "basic";
  if (strcmp(mode, "basic") == 0)
    basic(mode);
  else if (strcmp(mode, "prepare") == 0)
    populate(mode);
  else if (strcmp(mode, "check") == 0)
    verify(mode);
  else if (strcmp(mode, "compact") == 0)
    compact(mode);
  else if (strcmp(mode, "churn") == 0)
    churn(mode);
  else if (strcmp(mode, "refuse") == 0)
    refuse(mode);
  else if (strcmp(mode, "newchunk") == 0)
    newchunk(mode, 1);
  else if (strcmp(mode, "newchunk-check") == 0)
    newchunk(mode, 0);
  else if (strcmp(mode, "rollback") == 0)
    rollback(mode);
  else if (strcmp(mode, "nospace") == 0)
    nospace(mode);
  else if (strcmp(mode, "fault") == 0 && argc == 4) {
    CHECK(pmfs_fault(atoi(argv[2]), atoi(argv[3])) == 0, 22);
    pmfs_compact();
    CHECK(0, 23);
  } else {
    CHECK(0, 99);
  }
  printf("PMFS_GC_PASS mode=%s\n", mode);
  exit(0);
}
