#!/usr/bin/env python3
"""Check implemented native API regressions; this is NOT the parity gate."""
import argparse
from pathlib import Path
import sys

from fs_parity_gate import ReportError, parse_report

# This list only protects the native behavior currently exercised. The full
# gate additionally requires all API cases, device identity and crash oracles.
NATIVE_REGRESSIONS = (
    "files", "holes", "links", "directories", "long_path", "many_inodes",
    "copy_fault", "append_atomic", "size_limit", "devices", "exec", "sync_calls",
)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args(argv)
    try:
        report = parse_report(args.report.read_bytes())
        failures = [(case, report.cases[case]) for case in NATIVE_REGRESSIONS
                    if report.cases[case][0] != "PASS"]
        if failures:
            for case, (state, code) in failures:
                print(f"NATIVE_API_REGRESSION_FAIL case={case} state={state} code={code}")
            return 1
        print(f"NATIVE_API_REGRESSION_PASS cases={len(NATIVE_REGRESSIONS)} "
              f"other_fail={report.failed} blocked={report.blocked} (NOT parity)")
        return 0
    except (OSError, ReportError) as exc:
        print(f"NATIVE_API_REGRESSION_INVALID: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
