import unittest
from bench_gc_hints import paired_summary


class PairedTests(unittest.TestCase):
    def test_ratios_use_matched_boots(self):
        common = {"mode": "overwrite", "size": 64, "run": 0, "warmup": False}
        batches = [dict(common, arm="enabled", count=2, pm_ticks=40),
                   dict(common, arm="disabled", count=2, pm_ticks=80)]
        rows = [dict(common, arm="enabled", pm_ticks=10),
                dict(common, arm="disabled", pm_ticks=20)]
        result = paired_summary(rows, batches, seed=1, repetitions=10)["overwrite:64"]
        self.assertEqual(result["batch_throughput_enabled_over_disabled"], 2)
        self.assertEqual(result["median_per_boot_p50_latency_enabled_over_disabled"], 0.5)

    def test_unmatched_counts_rejected(self):
        common = {"mode": "overwrite", "size": 64, "run": 0, "warmup": False}
        batches = [dict(common, arm="enabled", count=2, pm_ticks=40),
                   dict(common, arm="disabled", count=3, pm_ticks=80)]
        rows = [dict(common, arm=arm, pm_ticks=10) for arm in ("enabled", "disabled")]
        with self.assertRaises(ValueError):
            paired_summary(rows, batches, seed=1, repetitions=10)


if __name__ == "__main__":
    unittest.main()
