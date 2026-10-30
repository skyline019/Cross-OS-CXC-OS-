#!/usr/bin/env python3
"""Bounded model of the proposed *no-KV, no-CDC* direct PMEM design.

Models one file update: COW data, inode and an immutable checkpoint record are
persisted before one failure-atomic store replaces the inactive root pointer.
At every operation boundary the crash oracle explores every subset of dirty
abstract lines that may have been evicted early. The model also checks a small
shared-DAG reference-count example and deliberate mutation controls.

This is an independent specification, NOT kernel C, a hardware trace, or a
proof for arbitrary workloads. It assumes an aligned failure-atomic root word,
correct flush/fence behavior, and collision-free metadata checksums. Each
object is split into only two modeled lines. Data is checked by an external
expected-content oracle; the proposed fast format need not checksum payloads.
It does not model concurrency, large radix trees, real devices, bit rot, or
all forms of persistent-memory ordering.
"""
from dataclasses import dataclass
from itertools import combinations


@dataclass(frozen=True)
class Checkpoint:
    identity: str
    epoch: int
    inode: int


@dataclass(frozen=True)
class Inode:
    identity: str
    data: int


EXPECTED = {
    1: ("older-left", "older-right"),
    2: ("old-left", "old-right"),
    3: ("new-left", "new-right"),
}


def cell(obj, part):
    return f"p{obj}:{part}"


def slot_word(checkpoint):
    # Idealized checksum over the pointer, not over the pointed-to record.
    return ("checked-pointer", checkpoint)


def object_lines(kind, obj):
    # The paired abstract lines stand for a checksummed immutable object.
    return ("head", kind, obj.identity), ("body", kind, obj)


def put_control(image, obj_id, kind, obj):
    image[cell(obj_id, 0)], image[cell(obj_id, 1)] = object_lines(kind, obj)


def read_control(image, obj_id, kind, cls):
    head, body = image.get(cell(obj_id, 0)), image.get(cell(obj_id, 1))
    if not isinstance(body, tuple) or len(body) != 3 or body[:2] != ("body", kind):
        return None
    obj = body[2]
    if not isinstance(obj, cls) or head != ("head", kind, obj.identity):
        return None
    return obj


def initial_image():
    # Slot lines are outside the object pool. Active root occupies objects 0-2;
    # the older root occupies 3-5; objects 6-8 are free for this transaction.
    image = {cell(p, i): None for p in range(9) for i in range(2)}
    image.update({"slot_a": slot_word(2), "slot_b": slot_word(5)})
    image[cell(0, 0)], image[cell(0, 1)] = EXPECTED[2]
    put_control(image, 1, "inode", Inode("old-inode", 0))
    put_control(image, 2, "checkpoint", Checkpoint("old-cp", 2, 1))
    image[cell(3, 0)], image[cell(3, 1)] = EXPECTED[1]
    put_control(image, 4, "inode", Inode("older-inode", 3))
    put_control(image, 5, "checkpoint", Checkpoint("older-cp", 1, 4))
    return image


def decode_root(image, slot):
    raw = image[slot]
    if not isinstance(raw, tuple) or len(raw) != 2 or raw != slot_word(raw[1]):
        return None
    cp = read_control(image, raw[1], "checkpoint", Checkpoint)
    if cp is None:
        return None
    inode = read_control(image, cp.inode, "inode", Inode)
    if inode is None:
        return None
    return cp, (image.get(cell(inode.data, 0)), image.get(cell(inode.data, 1)))


def check_image(image, acked):
    valid = []
    for slot in ("slot_a", "slot_b"):
        decoded = decode_root(image, slot)
        if decoded is None:
            return f"{slot} points to incomplete/corrupt metadata"
        cp, file_bytes = decoded
        if file_bytes != EXPECTED.get(cp.epoch):
            return f"{slot} epoch={cp.epoch} bytes={file_bytes!r}"
        valid.append((cp.epoch, image[slot][1]))
    if valid[0][0] == valid[1][0] and valid[0][1] != valid[1][1]:
        return "ambiguous-equal-epoch-different-checkpoints"
    if acked and max(valid)[0] != 3:
        return "acknowledged-checkpoint-not-durable"
    return None


def allocation(variant):
    # IDs stand for separate COW allocations, not a particular device layout.
    if variant == "overwrite_older":
        return 3, 7, 8
    if variant == "alias_staged":
        return 6, 6, 8
    return 6, 7, 8


def operations(variant):
    data, inode_id, cp_id = allocation(variant)
    inode = Inode("new-inode", data)
    cp = Checkpoint("new-cp", 3, inode_id)
    staged = []
    for kind, obj_id, parts in (
        ("data", data, EXPECTED[3]),
        ("inode", inode_id, object_lines("inode", inode)),
        ("checkpoint", cp_id, object_lines("checkpoint", cp)),
    ):
        for part, value in enumerate(parts):
            staged.append(("store", cell(obj_id, part), value))
            if not ((variant == "skip_data_flush" and kind == "data") or
                    (variant == "skip_checkpoint_flush" and kind == "checkpoint")):
                staged.append(("flush", cell(obj_id, part), None))
    staged.append(("fence", None, None))
    publish = [("store", "slot_b", slot_word(cp_id))]
    if variant != "skip_slot_flush":
        publish.append(("flush", "slot_b", None))
    publish.append(("fence", None, None))
    ops = publish + staged if variant == "early_publish" else staged + publish
    if variant == "discard_dirty_early":
        ops.insert(1, ("discard-dirty", None, None))
    return ops + [("ack", None, None)]


def subsets(items):
    for n in range(len(items) + 1):
        yield from combinations(items, n)


def explore(variant):
    volatile = initial_image()
    persistent = volatile.copy()
    requested = set()
    dirty_buffer = True
    acked = False
    checks = 0
    pre_ack_epochs = set()
    for step, (op, key, value) in enumerate([("start", None, None)] +
                                             operations(variant)):
        if op == "store":
            volatile[key] = value
        elif op == "flush":
            requested.add(key)
        elif op == "fence":
            for item in requested:
                persistent[item] = volatile[item]
            requested.clear()
        elif op == "discard-dirty":
            dirty_buffer = False
        elif op == "ack":
            dirty_buffer = False
            acked = True
        if not acked and not dirty_buffer:
            return checks, f"step={step} pre-ack-dirty-data-lost"
        dirty = [item for item in volatile if volatile[item] != persistent[item]]
        for evicted in subsets(dirty):
            crash = persistent.copy()
            for item in evicted:
                crash[item] = volatile[item]
            checks += 1
            failure = check_image(crash, acked)
            if failure:
                return checks, f"step={step} op={op} evicted={evicted} {failure}"
            if not acked:
                pre_ack_epochs.add(max(decode_root(crash, s)[0].epoch
                                       for s in ("slot_a", "slot_b")))
    if variant == "correct" and pre_ack_epochs != {2, 3}:
        return checks, f"expected old/new pre-ack outcomes, got {pre_ack_epochs}"
    return checks, None


def check_refresh():
    """An atomic older-slot refresh allocates no new checkpoint object."""
    volatile = initial_image()
    persistent = volatile.copy()
    requested = set()
    checks = 0
    ops = [("start", None), ("store", "slot_b"), ("flush", "slot_b"),
           ("fence", None), ("ack", None)]
    for op, key in ops:
        if op == "store":
            volatile[key] = volatile["slot_a"]
        elif op == "flush":
            requested.add(key)
        elif op == "fence":
            for item in requested:
                persistent[item] = volatile[item]
            requested.clear()
        dirty = [item for item in volatile if volatile[item] != persistent[item]]
        for evicted in subsets(dirty):
            crash = persistent.copy()
            for item in evicted:
                crash[item] = volatile[item]
            failure = check_image(crash, False)
            checks += 1
            if failure:
                raise ValueError(f"refresh {op}: {failure}")
            if op == "ack" and crash["slot_a"] != crash["slot_b"]:
                raise ValueError("acknowledged refresh did not persist")
    # Observing the volatile new pointer does NOT justify reclaiming an old
    # root before its slot update is durable: an eviction can persist the
    # overwritten old data while the slot itself remains old.
    bad = initial_image()
    bad[cell(3, 0)] = "reused-early"
    if check_image(bad, False) is None:
        raise ValueError("early-refresh-reuse mutation not detected")
    return checks


def rebuilt_counts(graph, roots):
    """Count actual incoming root/parent edges; visit a shared parent once."""
    counts = {node: 0 for node in graph}
    pending = []
    seen = set()
    for root in roots:
        counts[root] += 1
        pending.append(root)
    while pending:
        parent = pending.pop()
        if parent in seen:
            continue
        seen.add(parent)
        for child in graph[parent]:
            counts[child] += 1
            pending.append(child)
    return counts


def release(counts, graph, node):
    counts[node] -= 1
    if counts[node] < 0:
        raise ValueError("negative reference count")
    if counts[node] == 0:
        for child in graph[node]:
            release(counts, graph, child)


def check_references():
    # Two generations share C. N and ND are newly staged objects.
    graph = {"C": (), "AD": (), "BD": (), "ND": (),
             "A": ("C", "AD"), "B": ("C", "BD"), "N": ("C", "ND")}
    counts = rebuilt_counts(graph, ("A", "B"))
    # Install the new immutable objects and root, then retire B's slot.
    for child in graph["N"]:
        counts[child] += 1
    counts["N"] += 1
    release(counts, graph, "B")
    if counts != rebuilt_counts(graph, ("A", "N")):
        raise ValueError("incremental counts differ after commit")
    # Refresh the older slot to the same immutable checkpoint; no new object
    # is allocated. N now has two root references but its edges count once.
    counts["N"] += 1
    release(counts, graph, "A")
    if counts != rebuilt_counts(graph, ("N", "N")):
        raise ValueError("incremental counts differ after slot refresh")
    if counts["C"] != 1 or counts["AD"] != 0 or counts["BD"] != 0:
        raise ValueError("shared child or retired data mishandled")
    # Retiring only one of two roots naming N must leave N's edges intact.
    correct = counts.copy()
    release(correct, graph, "N")
    expected = rebuilt_counts(graph, ("N",))
    if correct != expected:
        raise ValueError("duplicate-root edge release incorrect")
    # A broken implementation also decrements shared child C on that release.
    broken = counts.copy()
    broken["N"] -= 1
    broken["C"] -= 1
    if broken == expected or broken["C"] != 0 or expected["C"] != 1:
        raise ValueError("shared-parent negative control not detected")


def main():
    checks, failure = explore("correct")
    if failure:
        print(f"DIRECT_DESIGN_MODEL_FAIL checks={checks} {failure}")
        return 1
    print(f"DIRECT_DESIGN_MODEL_PASS crash_images={checks} "
          "early_eviction=all_dirty_subsets scope=one-bounded-transaction")
    print("DIRECT_DESIGN_MODEL_PASS pre_ack_crash_outcomes=old,new")
    refresh_checks = check_refresh()
    print(f"DIRECT_DESIGN_MODEL_PASS refresh_crash_images={refresh_checks} "
          "slot_refresh=duplicate-root-without-allocation")
    print("DIRECT_DESIGN_MODEL_NEG_PASS mutation=reuse-before-refresh-durable")
    check_references()
    print("DIRECT_DESIGN_MODEL_PASS shared_dag=incremental-vs-rebuild")
    print("DIRECT_DESIGN_MODEL_NEG_PASS mutation=drop-shared-child-per-root")
    for variant in ("overwrite_older", "alias_staged", "skip_data_flush",
                    "skip_checkpoint_flush", "early_publish", "skip_slot_flush",
                    "discard_dirty_early"):
        checks, failure = explore(variant)
        if failure is None:
            print(f"DIRECT_DESIGN_MODEL_FAIL mutation={variant} not-detected checks={checks}")
            return 1
        print(f"DIRECT_DESIGN_MODEL_NEG_PASS mutation={variant} checks={checks} {failure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
