#!/usr/bin/env python3
"""Compatibility entry point for one EMUL smoke point; does not rewrite docs."""
import sys
from run_bench_sweep import main

if __name__ == "__main__":
    sys.exit(main(["--sizes", "32", "--emul-only"]))
