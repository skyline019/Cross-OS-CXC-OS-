// Software work counters and correctness regression; not hardware wear data.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/pmstats.h"
#include "user/user.h"

#define BASE 0x1200000000000000UL
#define TEST(c, code)                                                          \
  do {                                                                         \
    if (!(c)) {                                                                \
      printf("PMFS_EFFICIENCY_FAIL code=%d\n", code);                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static char val[80], got[80], big[1024];

int
main(void)
{
  struct pmfs_metrics a, b;
  unsigned int len;
  int fd, i;

  TEST(pmfs_metrics((void *)-1, sizeof(a)) < 0 &&
         pmfs_metrics(&a, __builtin_offsetof(struct pmfs_metrics, gc_scans) -
                            1) < 0 &&
         pmfs_metrics(&a, __builtin_offsetof(struct pmfs_metrics, gc_scans)) ==
           0 &&
         pmfs_metrics(&a, -1) < 0 && mkdir("/pm/wa") == 0,
       1);
  fd = open("/pm/wa/f", O_CREATE | O_RDWR);
  TEST(fd >= 0 && pmfs_metrics(&a, sizeof(a)) == 0 &&
         write(fd, "same", 4) == 4 && fsync(fd) == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.file_cdc_attempts == a.file_cdc_attempts + 1 &&
         b.dev_page_writes - a.dev_page_writes == 2 &&
         b.dev_write_bytes - a.dev_write_bytes == 8192 + 2 * (48 + 16) &&
         b.checkpoint_word_stores - a.checkpoint_word_stores == 4,
       15);
  printf("PMFS_METRIC case=file_sync page_writes=%lu dev_bytes=%lu "
         "checkpoint_word_stores=%lu\n",
         b.dev_page_writes - a.dev_page_writes,
         b.dev_write_bytes - a.dev_write_bytes,
         b.checkpoint_word_stores - a.checkpoint_word_stores);
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 && a.version == 1 &&
         lseek(fd, 0, SEEK_SET) == 0 && write(fd, "same", 4) == 4 &&
         fsync(fd) == 0 && pmfs_metrics(&b, sizeof(b)) == 0,
       2);
  TEST(b.file_write_bytes - a.file_write_bytes == 4 &&
         b.file_identical_bytes - a.file_identical_bytes == 4 &&
         b.file_identical_ops - a.file_identical_ops == 1 &&
         b.dev_page_writes == a.dev_page_writes &&
         b.dev_write_bytes == a.dev_write_bytes &&
         b.checkpoint_word_stores == a.checkpoint_word_stores &&
         b.requested_flush_lines == a.requested_flush_lines,
       3);
  printf("PMFS_METRIC case=identical user_bytes=%lu avoided=%lu dev_bytes=%lu "
         "page_writes=%lu\n",
         b.file_write_bytes - a.file_write_bytes,
         b.file_identical_bytes - a.file_identical_bytes,
         b.dev_write_bytes - a.dev_write_bytes,
         b.dev_page_writes - a.dev_page_writes);
  // A repeated write must not clear an already dirty version.
  TEST(lseek(fd, 0, SEEK_SET) == 0 && write(fd, "new!", 4) == 4 &&
         lseek(fd, 0, SEEK_SET) == 0 && write(fd, "new!", 4) == 4 &&
         fsync(fd) == 0 && lseek(fd, 0, SEEK_SET) == 0 &&
         read(fd, got, 4) == 4 && memcmp(got, "new!", 4) == 0 && close(fd) == 0,
       4);

  // Truncating a clean empty file must not publish another checkpoint.
  fd = open("/pm/wa/empty", O_CREATE | O_WRONLY);
  TEST(fd >= 0 && close(fd) == 0 && pmfs_metrics(&a, sizeof(a)) == 0, 5);
  fd = open("/pm/wa/empty", O_WRONLY | O_TRUNC);
  TEST(fd >= 0 && close(fd) == 0 && pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.dev_write_bytes == a.dev_write_bytes &&
         b.checkpoint_word_stores == a.checkpoint_word_stores,
       6);

  // This all-zero 1024-byte buffer makes CDC cut into more than four chunks;
  // bounded adaptation must fall back to fixed chunks and preserve all data.
  TEST(pmfs_metrics(&a, sizeof(a)) == 0, 16);
  fd = open("/pm/wa/big", O_CREATE | O_RDWR);
  TEST(fd >= 0 && write(fd, big, sizeof(big)) == sizeof(big) &&
         fsync(fd) == 0 && pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.file_fixed_attempts == a.file_fixed_attempts + 1 &&
         lseek(fd, 0, SEEK_SET) == 0 &&
         read(fd, big, sizeof(big)) == sizeof(big) && close(fd) == 0,
       17);
  for (i = 0; i < sizeof(big); i++)
    TEST(big[i] == 0, 18);

  // This 1024-byte pattern has four CDC chunks, so it is accepted rather
  // than forced to fixed mode; verify the complete ordinary-fd readback.
  for (i = 0; i < sizeof(big); i++)
    big[i] = (char)(i * 17 + i % 7);
  fd = open("/pm/wa/big", O_RDWR | O_TRUNC);
  TEST(fd >= 0 && pmfs_metrics(&a, sizeof(a)) == 0 &&
         write(fd, big, sizeof(big)) == sizeof(big) && fsync(fd) == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.file_cdc_attempts - a.file_cdc_attempts == 1 &&
         b.file_cdc_planned_chunks - a.file_cdc_planned_chunks == 4 &&
         b.file_fixed_attempts == a.file_fixed_attempts &&
         lseek(fd, 0, SEEK_SET) == 0,
       20);
  memset(big, 0, sizeof(big));
  TEST(read(fd, big, sizeof(big)) == sizeof(big) && close(fd) == 0, 21);
  for (i = 0; i < sizeof(big); i++)
    TEST((unsigned char)big[i] == (unsigned char)(i * 17 + i % 7), 22);
  printf("PMFS_METRIC case=cdc_bound payload=1024 planned_chunks=%lu\n",
         b.file_cdc_planned_chunks - a.file_cdc_planned_chunks);

  // A move without replacement modifies only the dentry snapshot. Replacing
  // another inode still needs both snapshots in the same checkpoint.
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 &&
         rename("/pm/wa/f", "/pm/wa/g") == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.dev_page_writes - a.dev_page_writes == 1 &&
         b.dev_write_bytes - a.dev_write_bytes == 4096 + 48 + 16 &&
         b.checkpoint_word_stores - a.checkpoint_word_stores == 2,
       7);
  printf("PMFS_METRIC case=rename page_writes=%lu dev_bytes=%lu "
         "checkpoint_word_stores=%lu\n",
         b.dev_page_writes - a.dev_page_writes,
         b.dev_write_bytes - a.dev_write_bytes,
         b.checkpoint_word_stores - a.checkpoint_word_stores);
  fd = open("/pm/wa/target", O_CREATE | O_WRONLY);
  TEST(fd >= 0 && close(fd) == 0 && pmfs_metrics(&a, sizeof(a)) == 0 &&
         rename("/pm/wa/g", "/pm/wa/target") == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.dev_page_writes - a.dev_page_writes == 2 &&
         b.dev_write_bytes - a.dev_write_bytes == 8192 + 48 + 16,
       8);
  printf("PMFS_METRIC case=replace page_writes=%lu dev_bytes=%lu\n",
         b.dev_page_writes - a.dev_page_writes,
         b.dev_write_bytes - a.dev_write_bytes);
  fd = open("/pm/wa/target", O_RDONLY);
  TEST(fd >= 0 && read(fd, got, 4) == 4 && memcmp(got, "new!", 4) == 0 &&
         close(fd) == 0,
       9);

  // Fill beyond the 512-key exact index, while retaining the KV/CDC core.
  // Values exceed the 64-byte value cache to expose each cold query route.
  memset(val, 'A', sizeof(val));
  for (i = 0; i < 576; i++) {
    TEST(pmfs_put(BASE + i, val, sizeof(val)) == 0, 10);
    // More than 128 runs exercise newest-first scans across posting words.
    if (i % 4 == 3)
      TEST(pmfs_seal() == 0, 11);
  }
  // Query a tracked key in the oldest of many runs without value caching.
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(BASE + 1, got, sizeof(got), &len) == 1 &&
         len == sizeof(got) && got[0] == 'A' &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_exact_hits - a.kv_exact_hits == 1 &&
         b.kv_fallback_run_probes == a.kv_fallback_run_probes &&
         b.kv_page_reads - a.kv_page_reads == 1,
       19);
  printf(
    "PMFS_METRIC case=tracked_old exact=%lu run_probes=%lu page_reads=%lu\n",
    b.kv_exact_hits - a.kv_exact_hits,
    b.kv_fallback_run_probes - a.kv_fallback_run_probes,
    b.kv_page_reads - a.kv_page_reads);

  // An older tracked key updated after index saturation must route to the
  // newest run, while the memtable shadows the old version before seal.
  memset(val, 'Z', sizeof(val));
  TEST(pmfs_put(BASE, val, sizeof(val)) == 0 &&
         pmfs_get(BASE, got, sizeof(got), &len) == 1 && len == sizeof(val) &&
         memcmp(got, val, len) == 0 && pmfs_seal() == 0 &&
         pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(BASE, got, sizeof(got), &len) == 1 && len == sizeof(val) &&
         memcmp(got, val, len) == 0 && pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_exact_hits - a.kv_exact_hits == 1 &&
         b.kv_fallback_run_probes == a.kv_fallback_run_probes &&
         b.kv_page_reads - a.kv_page_reads == 1,
       12);
  printf("PMFS_METRIC case=tracked exact=%lu run_probes=%lu page_reads=%lu\n",
         b.kv_exact_hits - a.kv_exact_hits,
         b.kv_fallback_run_probes - a.kv_fallback_run_probes,
         b.kv_page_reads - a.kv_page_reads);
  // A key omitted when the bounded exact table fills must still be found via
  // the complete newest-first fallback, not mistaken for a negative lookup.
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(BASE + 575, got, sizeof(got), &len) == 1 &&
         len == sizeof(got) && got[0] == 'A' &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_exact_hits == a.kv_exact_hits &&
         b.kv_fallback_run_probes > a.kv_fallback_run_probes,
       13);
  printf("PMFS_METRIC case=overflow exact=%lu run_probes=%lu page_reads=%lu\n",
         b.kv_exact_hits - a.kv_exact_hits,
         b.kv_fallback_run_probes - a.kv_fallback_run_probes,
         b.kv_page_reads - a.kv_page_reads);
  // Add a full posting word of unrelated runs, exercising sparse scans and
  // placing the next update in a different word from the original key.
  for (i = 0; i < 64; i++)
    TEST(pmfs_put(0x5600000000000000UL + i, val, sizeof(val)) == 0 &&
           pmfs_seal() == 0,
         24);
  // A non-indexed key updated after saturation must select its newest run,
  // including when the newer and older runs span different 64-bit words.
  memset(val, 'Y', sizeof(val));
  TEST(pmfs_put(BASE + 520, val, sizeof(val)) == 0 && pmfs_seal() == 0 &&
         pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(BASE + 520, got, sizeof(got), &len) == 1 &&
         len == sizeof(val) && memcmp(got, val, len) == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_exact_hits == a.kv_exact_hits &&
         b.kv_fallback_run_probes - a.kv_fallback_run_probes == 1,
       23);
  printf("PMFS_METRIC case=overflow_updated run_probes=%lu page_reads=%lu\n",
         b.kv_fallback_run_probes - a.kv_fallback_run_probes,
         b.kv_page_reads - a.kv_page_reads);
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(BASE + 521, got, sizeof(got), &len) == 1 &&
         len == sizeof(got) && got[0] == 'A' &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_exact_hits == a.kv_exact_hits &&
         b.kv_posting_skips - a.kv_posting_skips >= 64 &&
         b.kv_fallback_run_probes > a.kv_fallback_run_probes &&
         b.kv_page_reads - a.kv_page_reads == 1,
       25);
  printf("PMFS_METRIC case=sparse_overflow skipped=%lu run_probes=%lu "
         "page_reads=%lu\n",
         b.kv_posting_skips - a.kv_posting_skips,
         b.kv_fallback_run_probes - a.kv_fallback_run_probes,
         b.kv_page_reads - a.kv_page_reads);
  // Coarse posting bits are populated even for overflow keys: an unused
  // bucket skips run reads, and repeated absent lookup may use negative cache.
  TEST(pmfs_metrics(&a, sizeof(a)) == 0 &&
         pmfs_get(0x3400000000000001UL, got, sizeof(got), &len) == 0 &&
         pmfs_metrics(&b, sizeof(b)) == 0 &&
         b.kv_posting_skips > a.kv_posting_skips &&
         b.kv_fallback_run_probes == a.kv_fallback_run_probes &&
         b.kv_page_reads == a.kv_page_reads,
       14);
  printf(
    "PMFS_METRIC case=absent posting_skips=%lu run_probes=%lu page_reads=%lu\n",
    b.kv_posting_skips - a.kv_posting_skips,
    b.kv_fallback_run_probes - a.kv_fallback_run_probes,
    b.kv_page_reads - a.kv_page_reads);
  printf("PMFS_EFFICIENCY_PASS rename_pages=1 replace_pages=2 "
         "identical_overwrite=skipped cdc=adaptive index_overflow=fallback\n");
  exit(0);
}
