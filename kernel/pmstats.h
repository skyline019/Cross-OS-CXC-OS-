// Volatile software request counters, not physical-device wear telemetry.
#ifndef XV6_PMSTATS_H
#define XV6_PMSTATS_H

struct pmfs_metrics {
  uint64 version;         // 1; all counters are volatile, not on-media format
  uint64 dev_read_bytes;  // pmdev_read copies, excludes direct mappings
  uint64 dev_write_bytes; // pmdev_write plus direct checkpoint words
  uint64 dev_page_writes; // complete 4 KiB pmdev_write_page requests
  uint64 checkpoint_word_stores; // aligned 8-byte stores, both slots combined
  uint64 requested_flush_lines;  // software requests, not physical writebacks
  uint64 file_write_bytes;       // successful normal-fd payload bytes
  uint64 file_identical_bytes;   // bytes whose identical overwrite was skipped
  uint64 file_identical_ops;
  uint64 file_cdc_attempts; // attempts selecting CDC for a VFS buffer
  uint64
    file_cdc_planned_chunks;  // CDC-probed chunks, including failed attempts
  uint64 file_fixed_attempts; // fallback when CDC would add chunks
  uint64 kv_exact_hits;       // cold exact-index routes to a run page
  uint64 kv_fallback_run_probes;
  uint64 kv_posting_skips;
  uint64 kv_page_reads; // query path only, excludes recovery
};

#endif
