#!/usr/bin/env python3
"""Host-only checks for benchmark validation and reproducible statistics."""
import unittest

import bench_repro as bench


def serial(n=8, ticks=123):
    rows = [f"BENCH_TRIAL N={n} t={t} kv={ticks + t} file={ticks + t + 1} "
            f"xv6={ticks + t + 2} count_pmfs={n} count_xv6={n}"
            for t in range(5)]
    summary = (f"ONBOX_PMFS_BENCH_PASS trials=5 N={n} median_pmfs=1 "
               f"count_pmfs={n} count_xv6={n} units=r_time_ticks")
    return "\n".join(rows + [summary, "$ "])


class ParseTests(unittest.TestCase):
    def test_accepts_uint64_tick_values(self):
        rows = bench.parse_trials(serial(ticks=2**32 + 99), 8)
        self.assertEqual(len(rows), 5)
        self.assertEqual(rows[0]["kv_ticks"], 2**32 + 99)

    def test_rejects_missing_or_duplicate_trial(self):
        data = serial()
        with self.assertRaises(ValueError):
            bench.parse_trials(data.replace("t=4", "t=3"), 8)
        with self.assertRaises(ValueError):
            bench.parse_trials("\n".join(data.splitlines()[1:]), 8)

    def test_rejects_bad_counts_zero_and_extra_line(self):
        data = serial()
        for changed in (data.replace("count_xv6=8", "count_xv6=7", 1),
                        data.replace("kv=123", "kv=0", 1),
                        data + "\nBENCH_TRIAL invalid\n"):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                bench.parse_trials(changed, 8)

    def test_rejects_guest_failure_or_missing_summary(self):
        with self.assertRaises(ValueError):
            bench.parse_trials(serial() + "\nONBOX_PMFS_BENCH_FAIL\n", 8)
        with self.assertRaises(ValueError):
            bench.parse_trials(serial().replace("ONBOX_PMFS_BENCH_PASS", "BAD"), 8)


class StatisticsTests(unittest.TestCase):
    def records(self):
        rows = []
        for run in range(4):
            for trial in range(5):
                rows.append({"run": run, "trial": trial, "warmup": run == 0,
                             "n": 8, "kv_ticks": run * 10 + trial,
                             "pmfs_ticks": run * 100 + trial,
                             "xv6_ticks": run * 1000 + trial})
        return rows

    def test_groups_boots_excludes_warmup_and_is_reproducible(self):
        first = bench.summarize(self.records(), seed=123, samples=200)
        second = bench.summarize(self.records(), seed=123, samples=200)
        self.assertEqual(first, second)
        self.assertEqual(first["measured_boots"], 3)
        self.assertEqual(first["pmfs_ticks"]["boot_medians"], [102, 202, 302])
        self.assertEqual(first["pmfs_ticks"]["median"], 202)
        self.assertIn("small_sample_warning", first)

    def test_rejects_incomplete_or_mixed_runs(self):
        rows = self.records()
        with self.assertRaises(ValueError):
            bench.summarize(rows[:-1])
        rows[-1]["n"] = 16
        with self.assertRaises(ValueError):
            bench.summarize(rows)

    def test_linear_percentiles(self):
        self.assertEqual(bench.percentile([40, 10, 30, 20], 0.5), 25)
        self.assertEqual(bench.percentile([7], 0.975), 7)


if __name__ == "__main__":
    unittest.main()
