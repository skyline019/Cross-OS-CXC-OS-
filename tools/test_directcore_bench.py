import unittest

from directcore_bench import parse_output, summarize


class DirectCoreBenchmarkTests(unittest.TestCase):
    def fixture(self):
        return ("DIRECTBENCH_SAMPLE size=64 i=0 direct_ticks=10 xv_ticks=30 order=0\r\n"
                "DIRECTBENCH_SAMPLE size=64 i=1 direct_ticks=20 xv_ticks=40 order=1\r\n"
                "DIRECTBENCH_BATCH size=64 count=2 direct_ticks=35 xv_ticks=80 order=1\r\n"
                "DIRECTBENCH_PASS size=64 samples=2 warmup=4\r\n")

    def test_strict_parser(self):
        rows, batch = parse_output(self.fixture(), 64, 2, 1)
        self.assertEqual([r["i"] for r in rows], [0, 1])
        self.assertEqual(batch["direct_ticks"], 35)
        for changed in (self.fixture().replace("i=1", "i=0"),
                        self.fixture().replace("direct_ticks=20", "direct_ticks=0"),
                        self.fixture().replace("order=1\r\nDIRECTBENCH_PASS", "order=0\r\nDIRECTBENCH_PASS"),
                        self.fixture().replace("warmup=4", "warmup=5"),
                        self.fixture() + "DIRECTBENCH_FAIL stage=bad\n"):
            with self.assertRaises(ValueError):
                parse_output(changed, 64, 2, 1)

    def test_boot_grouped_summary_and_weighted_batch(self):
        rows, batches = [], []
        for run in range(3):
            for i in range(2):
                rows.append({"size": 64, "run": run, "warmup": run == 0,
                             "i": i, "direct_ticks": (run + 1) * 10,
                             "xv_ticks": (run + 1) * 20})
            batches.append({"size": 64, "run": run, "warmup": run == 0,
                            "count": 2, "direct_ticks": (run + 1) * 30,
                            "xv_ticks": (run + 1) * 60})
        result = summarize(rows, batches, 1000, repetitions=30)["64"]
        self.assertEqual(result["measured_boots"], 2)
        self.assertEqual(result["latency_samples_per_backend"], 4)
        self.assertAlmostEqual(result["direct_core"]["batch_transactions_per_guest_second"], 4000 / 150)
        self.assertIn("warning", result)
        with self.assertRaises(ValueError):
            summarize(rows, batches[:-1], 1000, repetitions=2)
        with self.assertRaises(ValueError):
            summarize(rows, batches, 0)


if __name__ == "__main__":
    unittest.main()
