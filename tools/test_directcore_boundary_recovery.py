import tempfile
from pathlib import Path
import unittest

from directcore_boundary_recovery import SIZES, active_view, make_cases
from directcore_trace_replay import expected_pattern


class DirectCoreBoundaryTests(unittest.TestCase):
    def test_boundaries_and_seed_are_reproducible(self):
        first = make_cases(24, 1234)
        self.assertEqual(first, make_cases(24, 1234))
        self.assertNotEqual(first, make_cases(24, 1235))
        self.assertEqual([case["size"] for case in first[:len(SIZES)]], list(SIZES))
        self.assertTrue(all(0 <= case["size"] <= 512 and case["generation"] >= 0 for case in first))
        with self.assertRaises(ValueError):
            make_cases(-1, 1)
        with self.assertRaises(ValueError):
            make_cases(1001, 1)

    def test_directed_noops_include_zero_and_wrapped_patterns(self):
        cases = make_cases(0, 1)
        payloads = [expected_pattern(case["generation"], case["size"]) for case in cases]
        noops = [i for i in range(1, len(payloads)) if payloads[i] == payloads[i - 1]]
        self.assertGreaterEqual(len(noops), 4)
        self.assertEqual(expected_pattern(10, 64), expected_pattern(266, 64))
        self.assertEqual(expected_pattern(1, 0), expected_pattern(257, 0))

    def test_rejects_short_media(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "short.pmem"
            path.write_bytes(bytes(8192))
            with self.assertRaises(ValueError):
                active_view(path, {1: b""})


if __name__ == "__main__":
    unittest.main()
