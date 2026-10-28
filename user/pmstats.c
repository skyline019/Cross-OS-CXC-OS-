// Read-only snapshot of volatile PMFS software work counters.
#include "kernel/types.h"
#include "kernel/pmstats.h"
#include "user/user.h"

int
main(void)
{
  struct pmfs_metrics m;
  if (pmfs_metrics(&m, sizeof(m)) != 0 || m.version != 1) {
    fprintf(2, "pmstats: PMFS unavailable or unsupported metrics version\n");
    exit(1);
  }
  printf(
    "PMFS_SOFTWARE_METRICS version=%lu (not physical wear or erase data)\n",
    m.version);
  printf("pmdev_read_bytes=%lu pmdev_write_bytes=%lu page_writes=%lu "
         "checkpoint_word_stores=%lu flush_line_requests=%lu\n",
         m.dev_read_bytes, m.dev_write_bytes, m.dev_page_writes,
         m.checkpoint_word_stores, m.requested_flush_lines);
  printf("file_write_bytes=%lu identical_bytes_skipped=%lu "
         "identical_ops_skipped=%lu cdc_attempts=%lu cdc_planned_chunks=%lu "
         "fixed_attempts=%lu\n",
         m.file_write_bytes, m.file_identical_bytes, m.file_identical_ops,
         m.file_cdc_attempts, m.file_cdc_planned_chunks, m.file_fixed_attempts);
  printf("kv_exact_hits=%lu fallback_run_probes=%lu posting_skips=%lu "
         "query_page_reads=%lu\n",
         m.kv_exact_hits, m.kv_fallback_run_probes, m.kv_posting_skips,
         m.kv_page_reads);
  printf("gc_scans=%lu gc_trace_runs=%lu gc_reused_pages=%lu "
         "gc_failed_scans=%lu tail_pages=%lu capacity_pages=%lu runs=%lu\n",
         m.gc_scans, m.gc_trace_runs, m.gc_reused_pages, m.gc_failed_scans,
         m.fs_tail_pages, m.fs_capacity_pages, m.kv_runs);
  printf(
    "compact_calls=%lu compact_input_records=%lu compact_output_records=%lu "
    "compact_output_pages=%lu\n",
    m.compact_calls, m.compact_input_records, m.compact_output_records,
    m.compact_output_pages);
  exit(0);
}
