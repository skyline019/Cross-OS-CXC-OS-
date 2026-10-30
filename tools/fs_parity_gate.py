#!/usr/bin/env python3
"""Fail-closed acceptance gate for full native/direct filesystem parity.

The guest API report is only one necessary input. Hardware identity and
persistence validators are intentionally marked unimplemented below; an API
smoke report, a synthetic directcore syscall, or two identical reports cannot
make this gate pass. Add independent executable validators before removing any
blocker; a claimed PASS in a JSON document is not a recovery oracle.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys

API_CASES = (
    "files", "holes", "links", "directories", "rename", "long_path",
    "long_name", "many_inodes", "copy_fault", "append_atomic", "size_limit",
    "devices", "exec", "sync_calls", "symlink", "truncate_nonzero", "mmap",
    "permissions", "dynamic_mount",
)

# Deliberately no claimed implementation. Each item needs a real validator,
# against each backend, with its own negative controls and preserved evidence.
# QEMU process-kill and a stated modeled persistence domain are the current
# target; none of these claim physical power-failure verification.
UNIMPLEMENTED_ORACLES = (
    "identity_nvme_and_independent_direct_nvdimm_vfs_no_kv_cdc",
    "resource_limits_and_near_full",
    "fd_fsync_acknowledged_data_and_metadata",
    "directory_fsync_and_namespace_dependencies",
    "global_sync_and_concurrent_barriers",
    "unsynced_allowed_states_and_successful_write_boundaries",
    "atomic_rename_link_unlink_and_open_orphans",
    "short_write_enospc_and_error_propagation",
    "concurrent_readers_writers_reclamation_and_append",
    "actual_store_flush_fence_fault_replay_and_negative_controls",
)

BEGIN = re.compile(r"^FS_PARITY_BEGIN version=1 root=(/[^\s]*)$")
CASE = re.compile(r"^FS_PARITY_CASE id=([a-z0-9_]+) result=(PASS|FAIL|BLOCKED) code=(-?\d+)$")
END = re.compile(r"^FS_PARITY_END total=(\d+) pass=(\d+) fail=(\d+) blocked=(\d+)$")


class ReportError(ValueError):
    pass


@dataclass(frozen=True)
class Report:
    root: str
    cases: dict
    passed: int
    failed: int
    blocked: int


def parse_report(data):
    if isinstance(data, bytes):
        try:
            data = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ReportError("report is not UTF-8") from exc
    begins, ends, cases = [], [], {}
    seen_end = False
    for record in data.splitlines(keepends=True):
        if record.startswith("FS_PARITY_") and not record.endswith("\n"):
            raise ReportError("truncated serial marker")
        line = record.rstrip("\r\n")
        if line.startswith("FS_PARITY_BEGIN"):
            m = BEGIN.fullmatch(line)
            if not m or begins or cases or ends:
                raise ReportError("invalid or duplicate report start")
            begins.append(m.group(1))
        elif line.startswith("FS_PARITY_CASE"):
            m = CASE.fullmatch(line)
            if not m or not begins or seen_end:
                raise ReportError("malformed or out-of-order case")
            case, result, raw_code = m.groups()
            code = int(raw_code)
            if case not in API_CASES or case in cases:
                raise ReportError(f"unknown or duplicate case {case}")
            if (result in ("PASS", "BLOCKED") and code != 0) or (result == "FAIL" and code == 0):
                raise ReportError(f"inconsistent result/code for {case}")
            cases[case] = (result, code)
        elif line.startswith("FS_PARITY_END"):
            m = END.fullmatch(line)
            if not m or not begins or ends:
                raise ReportError("invalid or duplicate report end")
            ends.append(tuple(map(int, m.groups())))
            seen_end = True
        elif line.startswith("FS_PARITY_SETUP_FAIL"):
            raise ReportError("guest setup failed")
    if len(begins) != 1 or len(ends) != 1:
        raise ReportError("incomplete guest report")
    if set(cases) != set(API_CASES):
        raise ReportError("missing cases: " + ", ".join(sorted(set(API_CASES) - set(cases))))
    counts = tuple(sum(result == wanted for result, _ in cases.values())
                   for wanted in ("PASS", "FAIL", "BLOCKED"))
    if ends[0] != (len(API_CASES), *counts):
        raise ReportError("summary does not match case records")
    return Report(begins[0], cases, *counts)


def blockers(native, direct):
    result = []
    for label, report in (("native", native), ("direct", direct)):
        for case in API_CASES:
            state, code = report.cases[case]
            if state != "PASS":
                result.append(f"{label}: {case}={state} code={code}")
    result.extend("missing independent oracle: " + name for name in UNIMPLEMENTED_ORACLES)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-report", type=Path, required=True)
    parser.add_argument("--direct-report", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.native_report.resolve() == args.direct_report.resolve():
            raise ReportError("native and direct reports must be separate files")
        native = parse_report(args.native_report.read_bytes())
        direct = parse_report(args.direct_report.read_bytes())
        problems = blockers(native, direct)
        if problems:
            for item in problems:
                print("BLOCKED " + item)
            print(f"FS_PARITY_GATE_BLOCKED count={len(problems)}")
            return 1
        print("FS_PARITY_GATE_PASS")
        return 0
    except (OSError, ReportError) as exc:
        print(f"FS_PARITY_GATE_INVALID: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
