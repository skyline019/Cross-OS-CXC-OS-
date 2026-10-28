#!/usr/bin/env python3
"""Bounded two-slot reclamation model; not a trace or proof of kernel C.

Assumes atomic commit words, durable old pages, and correctly ordered page
persistence. Explore candidate reuse before and after publication, including
forced fallback to either still-valid checkpoint.
"""

from itertools import combinations


def sets(pages):
    for size in range(1, 4):
        for values in combinations(pages, size):
            yield frozenset(values)


def recoverable(roots, overwritten):
    return not (roots & overwritten)


def main():
    pages = frozenset(range(6))
    states = checks = 0
    for older in sets(pages):
        for active in sets(pages):
            free = pages - (older | active)
            for new_page in free:
                states += 1
                # The new page may be partly persisted before the inactive
                # slot is invalidated. Both old slots remain recoverable.
                changed = {new_page}
                if not (recoverable(older, changed) and recoverable(active, changed)):
                    raise SystemExit("GC_MODEL_FAIL pre-publication protection")
                checks += 2
                # After invalidation only active is committed; after ordered
                # publication, active and the new root remain recoverable.
                if not recoverable(active, changed):
                    raise SystemExit("GC_MODEL_FAIL invalidation")
                new_roots = frozenset({new_page})
                # Writing the free page is the creation of new_roots, not its
                # destruction; no old root could alias it.
                if new_roots & active:
                    raise SystemExit("GC_MODEL_FAIL published root overlap")
                checks += 2
                # On the next transaction the formerly active root is the
                # older slot and must still be protected until replacement.
                for next_page in pages - (active | new_roots):
                    if not (recoverable(active, {next_page}) and
                            recoverable(new_roots, {next_page})):
                        raise SystemExit("GC_MODEL_FAIL deferred release")
                    checks += 2

    if not states:
        raise SystemExit("GC_MODEL_FAIL empty state space")
    print(f"GC_MODEL_PASS allocations={states} root_checks={checks} "
          "slots=two model=bounded-not-kernel-trace")

    # A candidate in only the older checkpoint looks free to an allocator
    # that scans the active root alone; tearing active exposes the damage.
    older, active = frozenset({1, 2}), frozenset({2, 3})
    unsafe = 1
    if unsafe in active or recoverable(older, {unsafe}):
        raise SystemExit("GC_MODEL_FAIL inactive-slot mutation undetected")
    print(f"GC_MODEL_NEG_PASS mutation=ignore-inactive overwritten_page={unsafe}")

    # After compacting from B to disjoint C, B is still a valid fallback. It
    # cannot be recycled merely because C was published in the other slot.
    old_run, compacted = frozenset({1, 2}), frozenset({4, 5})
    if recoverable(old_run, {1}) or old_run & compacted:
        raise SystemExit("GC_MODEL_FAIL early-run-release mutation undetected")
    print("GC_MODEL_NEG_PASS mutation=release-old-run-before-slot-retirement")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
