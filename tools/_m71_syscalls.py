#!/usr/bin/env python3
"""Extract likely Linux syscall numbers preceding `syscall` in an ELF dump."""
from __future__ import annotations

import re
import subprocess
import sys

NAMES = {
    0: "read",
    1: "write",
    2: "open",
    3: "close",
    4: "stat",
    5: "fstat",
    8: "lseek",
    9: "mmap",
    10: "mprotect",
    11: "munmap",
    12: "brk",
    13: "rt_sigaction",
    14: "rt_sigprocmask",
    16: "ioctl",
    20: "writev",
    21: "access",
    39: "getpid",
    60: "exit",
    63: "uname",
    72: "fcntl",
    79: "getcwd",
    83: "mkdir",
    89: "readlink",
    96: "gettimeofday",
    102: "getuid",
    107: "geteuid",
    158: "arch_prctl",
    186: "gettid",
    201: "time",
    202: "futex",
    217: "getdents64",
    218: "set_tid_address",
    231: "exit_group",
    257: "openat",
    262: "newfstatat",
    334: "rseq",
}


def main() -> int:
    path = sys.argv[1]
    dump = subprocess.check_output(["objdump", "-d", path], text=True)
    lines = dump.splitlines()
    nums: set[int] = set()
    mov_re = re.compile(r"mov\s+\$0x([0-9a-fA-F]+),%e?ax")
    for i, line in enumerate(lines):
        if "syscall" not in line:
            continue
        for j in range(i - 1, max(i - 16, -1), -1):
            match = mov_re.search(lines[j])
            if match:
                nums.add(int(match.group(1), 16))
                break
    print("syscalls:", sorted(nums))
    for num in sorted(nums):
        print(f"  {num}: {NAMES.get(num, '?')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
