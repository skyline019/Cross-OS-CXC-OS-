#!/usr/bin/env python3
"""Small executable *specification* of the v3 COW publication protocol.

This is not an instrumented run of the kernel. It explores every subset of
currently dirty modeled cache lines at each operation boundary, allowing early
writeback before flush. A fence makes all preceding flush requests persistent;
the aligned commit word is assumed failure-atomic. Three two-line data pages
and a two-line checkpoint are modeled. Checksums are collision-free in the
model, unlike real 32-bit checksums. Negative controls intentionally remove
required ordering. QEMU/hardware tests are separate and still required.
"""
from itertools import combinations
import sys

DATA = ("run0", "run1", "inode0", "inode1", "dent0", "dent1")
OLD_HEADER = (1, "old")
NEW_HEADER = (3, "new")


def tag(header):
    return ("valid-checksum", header)


def trace(variant):
    ops = []
    for page in (DATA[0:2], DATA[2:4], DATA[4:6]):
        for line in page:
            ops.append(("store", line, "new:" + line))
        if variant != "no_data_flush":
            ops.extend(("flush", line, None) for line in page)
        ops.append(("fence", None, None))
    ops += [("store", "word", None), ("flush", "word", None),
            ("fence", None, None), ("store", "header", NEW_HEADER),
            ("flush", "header", None), ("flush", "word", None),
            ("fence", None, None), ("store", "word", tag(NEW_HEADER)),
            ("flush", "word", None), ("fence", None, None),
            ("ack", None, None)]
    if variant == "no_commit_flush":
        # Acknowledge after a fence that did not request the commit word.
        del ops[-3]  # final commit-word flush
    if variant == "early_publish":
        # Valid checkpoint reaches the persistence domain before its pages.
        publish = [("store", "word", None), ("flush", "word", None),
                   ("fence", None, None), ("store", "header", NEW_HEADER),
                   ("flush", "header", None), ("fence", None, None),
                   ("store", "word", tag(NEW_HEADER)), ("flush", "word", None),
                   ("fence", None, None)]
        ops = publish + ops[:15] + [("ack", None, None)]
    return ops


def recover(persisted):
    # Slot A is the immutable valid old root with epoch 2.
    header = persisted["header"]
    if persisted["word"] != tag(header) or header[0] <= 2:
        return "old"
    if any(persisted[line] != "new:" + line for line in DATA):
        return "corrupt-new-root"
    return "new"


def subsets(items):
    for count in range(len(items) + 1):
        yield from combinations(items, count)


def explore(variant):
    volatile = {key: None for key in DATA}
    volatile.update(header=OLD_HEADER, word=tag(OLD_HEADER))
    persistent = volatile.copy()
    pending = set()
    checks = 0
    acked = False
    for step, (op, key, value) in enumerate([("start", None, None)] + trace(variant)):
        if op == "store":
            volatile[key] = value
        elif op == "flush":
            pending.add(key)
        elif op == "fence":
            for line in pending:
                persistent[line] = volatile[line]
            pending.clear()
        elif op == "ack":
            acked = True
        dirty = [line for line in volatile if volatile[line] != persistent[line]]
        for evicted in subsets(dirty):
            image = persistent.copy()
            for line in evicted:
                image[line] = volatile[line]
            result = recover(image)
            checks += 1
            if result not in ("old", "new") or (acked and result != "new"):
                return checks, (step, op, evicted, result, acked)
    return checks, None


def main():
    count, failure = explore("correct")
    if failure:
        print(f"MODEL_FAIL correct checks={count} counterexample={failure}")
        return 1
    print(f"MODEL_PASS protocol=cow checkpoints={count} early_eviction=all_subsets")
    for variant in ("no_data_flush", "early_publish", "no_commit_flush"):
        count, failure = explore(variant)
        if failure is None:
            print(f"MODEL_FAIL negative={variant} not_detected checks={count}")
            return 1
        print(f"MODEL_NEGATIVE_PASS mutation={variant} checks={count} counterexample={failure}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
