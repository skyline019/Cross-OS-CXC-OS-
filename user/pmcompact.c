// Explicit PMFS full-chain COW maintenance; never formats media.
#include "kernel/types.h"
#include "kernel/pmstats.h"
#include "user/user.h"

int
main(void)
{
  struct pmfs_metrics before, after;
  int rc;
  if (pmfs_metrics(&before, sizeof(before)) != 0) {
    fprintf(2, "pmcompact: PMFS unavailable\n");
    exit(1);
  }
  rc = pmfs_compact();
  if (rc != 0) {
    if (rc == PM_COMPACT_BUSY)
      fprintf(2, "pmcompact: pending KV entries or open file streams\n");
    else if (rc == PM_COMPACT_NOSPACE)
      fprintf(2, "pmcompact: insufficient unused tail space\n");
    else
      fprintf(2, "pmcompact: maintenance failed; inspect PMFS state\n");
    exit(1);
  }
  if (pmfs_metrics(&after, sizeof(after)) != 0) {
    fprintf(2, "pmcompact: metrics unavailable after maintenance\n");
    exit(1);
  }
  printf("PMFS_COMPACT_PASS runs_before=%lu runs_after=%lu "
         "input_records=%lu output_records=%lu output_pages=%lu "
         "tail_before=%lu tail_after=%lu read_bytes=%lu write_bytes=%lu\n",
         before.kv_runs, after.kv_runs,
         after.compact_input_records - before.compact_input_records,
         after.compact_output_records - before.compact_output_records,
         after.compact_output_pages - before.compact_output_pages,
         before.fs_tail_pages, after.fs_tail_pages,
         after.dev_read_bytes - before.dev_read_bytes,
         after.dev_write_bytes - before.dev_write_bytes);
  exit(0);
}
