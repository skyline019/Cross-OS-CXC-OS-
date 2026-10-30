"""Strict report parsing and no-false-parity negative controls."""
import unittest

from fs_parity_gate import API_CASES, UNIMPLEMENTED_ORACLES, ReportError, blockers, parse_report


def report(states=None, root="/test"):
    states = states or {}
    lines = [f"FS_PARITY_BEGIN version=1 root={root}"]
    totals = {"PASS": 0, "FAIL": 0, "BLOCKED": 0}
    for name in API_CASES:
        state = states.get(name, "PASS")
        totals[state] += 1
        lines.append(f"FS_PARITY_CASE id={name} result={state} code={17 if state == 'FAIL' else 0}")
    lines.append(f"FS_PARITY_END total={len(API_CASES)} pass={totals['PASS']} "
                 f"fail={totals['FAIL']} blocked={totals['BLOCKED']}")
    return "\n".join(lines) + "\n"


class ParityGateTests(unittest.TestCase):
    def test_even_complete_api_reports_cannot_bypass_persistence_or_identity(self):
        native = parse_report(report())
        direct = parse_report(report(root="/other"))
        problems = blockers(native, direct)
        self.assertEqual(len(problems), len(UNIMPLEMENTED_ORACLES))
        self.assertTrue(any("direct_nvdimm_vfs_no_kv_cdc" in p for p in problems))
        self.assertTrue(any("fd_fsync" in p for p in problems))

    def test_failure_and_blocked_are_hard_failures_for_each_backend(self):
        native = parse_report(report({"rename": "FAIL", "mmap": "BLOCKED"}))
        direct = parse_report(report({"holes": "FAIL"}, "/target"))
        problems = blockers(native, direct)
        self.assertIn("native: rename=FAIL code=17", problems)
        self.assertIn("native: mmap=BLOCKED code=0", problems)
        self.assertIn("direct: holes=FAIL code=17", problems)

    def test_missing_duplicate_and_unknown_cases_are_rejected(self):
        original = report()
        record = "FS_PARITY_CASE id=holes result=PASS code=0\n"
        for broken in (original.replace(record, ""), original.replace(record, record * 2),
                       original.replace("id=holes", "id=unknown")):
            with self.subTest(broken=broken):
                with self.assertRaises(ReportError):
                    parse_report(broken)

    def test_invalid_order_counts_and_codes_are_rejected(self):
        original = report()
        for broken in (original.replace("result=PASS code=0", "result=PASS code=3", 1),
                       original.replace(f"pass={len(API_CASES)}", "pass=0"),
                       original.replace("FS_PARITY_BEGIN", "FS_PARITY_BEGIN_BAD", 1),
                       original.replace("FS_PARITY_END", "FS_PARITY_END_BAD", 1),
                       original.rstrip("\n"),
                       original + "FS_PARITY_CASE id=files result=PASS code=0\n"):
            with self.subTest(broken=broken):
                with self.assertRaises(ReportError):
                    parse_report(broken)

    def test_unrelated_serial_noise_is_ignored_but_setup_fail_is_not(self):
        good = "qemu boot\n" + report() + "$ "
        self.assertEqual(parse_report(good).root, "/test")
        with self.assertRaises(ReportError):
            parse_report("FS_PARITY_SETUP_FAIL\n" + good)


if __name__ == "__main__":
    unittest.main()
