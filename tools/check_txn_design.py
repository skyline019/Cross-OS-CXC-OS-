#!/usr/bin/env python3
"""Bounded specification of the *proposed*, not implemented, v3 file commit.

A file update and an unrelated pending public KV are staged into one run,
with a new inode, then made reachable by one dual-slot checkpoint. The model
checks both committed roots, early eviction of every subset of dirty modeled
lines, successful-ack durability, pre-publication failure state, and an
ENOSPC preflight. It includes deliberate mutation controls.

This is an independent Python model, NOT execution of kernel C or v4. Each
page is reduced to two abstract lines; root headers and commit words are
modeled independently (a conservative over-approximation when they share a
real cache line). Checksums are collision-free here. Aligned commit-word
atomicity, an intact active slot, and the semantics of flush/fence are
assumptions. It does not prove real NVDIMM persistence or cover arbitrary
workloads, multiple transactions, readers, compaction, or bit corruption.
"""
from dataclasses import dataclass
from itertools import combinations
from typing import Optional


@dataclass(frozen=True)
class Root:
    epoch: int
    run: int
    inode: int


@dataclass(frozen=True)
class Run:
    identity: str
    key: str
    data: str
    public_kv: str
    previous: Optional[int]


@dataclass(frozen=True)
class Inode:
    identity: str
    key: str


ACTIVE = Root(2, 0, 1)
OLDER = Root(1, 2, 3)
EXPECTED = {
    1: ("older-file", "older-kv"),
    2: ("old-file", "old-kv"),
    3: ("new-file", "pending-kv"),
}
PENDING = {"public-key": "pending-kv"}
DRAFT = "new-file"


def word(root):
    # Idealized collision-free checksum bound to the entire checkpoint.
    return ("commit", root)


def line(page, part):
    return f"page{page}:{part}"


def object_lines(kind, obj):
    return ("head", kind, obj.identity), ("body", kind, obj)


def put_object(image, page, kind, obj):
    image[line(page, 0)], image[line(page, 1)] = object_lines(kind, obj)


def initial_image(npages=6):
    image = {line(p, i): None for p in range(npages) for i in range(2)}
    image.update({"a:header": ACTIVE, "a:word": word(ACTIVE),
                  "b:header": OLDER, "b:word": word(OLDER)})
    put_object(image, 0, "run", Run("old-run", "old-key", "old-file", "old-kv", None))
    put_object(image, 1, "inode", Inode("old-inode", "old-key"))
    put_object(image, 2, "run", Run("older-run", "older-key", "older-file", "older-kv", None))
    put_object(image, 3, "inode", Inode("older-inode", "older-key"))
    return image


def read_object(image, page, kind):
    head = image.get(line(page, 0))
    body = image.get(line(page, 1))
    if not isinstance(body, tuple) or len(body) != 3 or body[:2] != ("body", kind):
        return None
    obj = body[2]
    expected_type = Run if kind == "run" else Inode
    if not isinstance(obj, expected_type) or head != ("head", kind, obj.identity):
        return None
    return obj


def read_view(image, root):
    inode = read_object(image, root.inode, "inode")
    if inode is None:
        return None
    seen = set()
    page = root.run
    file_data = public_kv = None
    while page is not None:
        if page in seen:
            return None
        seen.add(page)
        run = read_object(image, page, "run")
        if run is None:
            return None
        if file_data is None and run.key == inode.key:
            file_data = run.data
        if public_kv is None:
            public_kv = run.public_kv
        page = run.previous
    if file_data is None or public_kv is None:
        return None
    return file_data, public_kv


def check_image(image, acked):
    valid = []
    for slot in ("a", "b"):
        root = image[f"{slot}:header"]
        if not isinstance(root, Root) or image[f"{slot}:word"] != word(root):
            continue
        view = read_view(image, root)
        if view != EXPECTED.get(root.epoch):
            return f"slot={slot} epoch={root.epoch} view={view!r}"
        valid.append(root)
    if not valid:
        return "no-valid-checkpoint"
    if acked and max(valid, key=lambda r: r.epoch).epoch != 3:
        return "acknowledged-new-root-not-durable"
    return None


def reserve(npages, variant="correct"):
    """Reserve run and inode jointly; no modeled state changes on failure."""
    protected = {ACTIVE.run, ACTIVE.inode}
    if variant != "ignore_older_root":
        protected.update({OLDER.run, OLDER.inode})
    available = [p for p in range(npages) if p not in protected]
    if len(available) < 2:
        return None
    if variant == "reuse_staged_page":
        return available[0], available[0]
    return available[0], available[1]


def operations(run_page, inode_page, variant):
    new_root = Root(3, run_page, inode_page)
    public_kv = "old-kv" if variant == "drop_public_kv" else "pending-kv"
    run = Run("new-run", "new-key", "new-file", public_kv, ACTIVE.run)
    inode = Inode("new-inode", "new-key")
    staged = []
    for page, kind, obj in ((run_page, "run", run), (inode_page, "inode", inode)):
        for part, value in enumerate(object_lines(kind, obj)):
            staged.append(("store", line(page, part), value))
            if not (variant == "skip_run_flush" and kind == "run"):
                staged.append(("flush", line(page, part), None))
    staged.append(("fence", None, None))
    publish = [
        ("store", "b:word", None),
        ("flush", "b:word", None),
        ("fence", None, None),
        ("store", "b:header", new_root),
        ("flush", "b:header", None),
        ("fence", None, None),
        ("store", "b:word", word(new_root)),
    ]
    if variant != "skip_commit_flush":
        publish.append(("flush", "b:word", None))
    publish.append(("fence", None, None))
    ops = publish + staged if variant == "early_publish" else staged + publish
    if variant == "clear_pending_early":
        ops.insert(1, ("clear-pending", None, None))
    return ops + [("ack", None, None)]


def subsets(items):
    for n in range(len(items) + 1):
        yield from combinations(items, n)


def explore(variant):
    allocation = reserve(6, variant)
    if allocation is None:
        return 0, "unexpected ENOSPC"
    volatile = initial_image()
    persistent = volatile.copy()
    flushed = set()
    pending = PENDING.copy()
    draft = DRAFT
    acked = False
    checks = 0
    pre_ack_epochs = set()
    for step, (op, key, value) in enumerate([("start", None, None)] +
                                             operations(*allocation, variant)):
        if op == "store":
            volatile[key] = value
        elif op == "flush":
            flushed.add(key)
        elif op == "fence":
            for item in flushed:
                persistent[item] = volatile[item]
            flushed.clear()
        elif op == "clear-pending":
            pending.clear()
        elif op == "ack":
            pending.clear()
            acked = True
        if not acked and (pending != PENDING or draft != DRAFT):
            return checks, f"step={step} op={op} pre-ack pending-or-draft-lost"
        dirty = [k for k in volatile if volatile[k] != persistent[k]]
        for evicted in subsets(dirty):
            candidate = persistent.copy()
            for item in evicted:
                candidate[item] = volatile[item]
            checks += 1
            failure = check_image(candidate, acked)
            if failure:
                return checks, f"step={step} op={op} evicted={evicted} {failure}"
            if not acked:
                epochs = [candidate[f"{slot}:header"].epoch for slot in ("a", "b")
                          if isinstance(candidate[f"{slot}:header"], Root) and
                          candidate[f"{slot}:word"] == word(candidate[f"{slot}:header"])]
                pre_ack_epochs.add(max(epochs))
    if variant == "correct" and pre_ack_epochs != {2, 3}:
        return checks, f"expected old/new pre-ack outcomes, got {pre_ack_epochs}"
    return checks, None


def main():
    count, failure = explore("correct")
    if failure is not None:
        print(f"TXN_DESIGN_MODEL_FAIL correct checks={count} {failure}")
        return 1
    print(f"TXN_DESIGN_MODEL_PASS crash_images={count} "
          "early_eviction=all_dirty_subsets scope=single-bounded-proposal")
    print("TXN_DESIGN_MODEL_PASS pre_ack_crash_outcomes=old,new")

    # With both roots protected only one page remains, but this transaction
    # requires two disjoint pages. A failed preflight leaves all state intact.
    image = initial_image(5)
    before_image = image.copy()
    pending = PENDING.copy()
    if reserve(5) is not None or image != before_image or pending != PENDING:
        print("TXN_DESIGN_MODEL_FAIL ENOSPC preflight")
        return 1
    print("TXN_DESIGN_MODEL_PASS enospc=preflight-no-state-change")

    mutations = ("ignore_older_root", "reuse_staged_page", "skip_run_flush",
                 "early_publish", "skip_commit_flush", "drop_public_kv",
                 "clear_pending_early")
    for variant in mutations:
        count, failure = explore(variant)
        if failure is None:
            print(f"TXN_DESIGN_MODEL_FAIL mutation={variant} not-detected checks={count}")
            return 1
        print(f"TXN_DESIGN_MODEL_NEG_PASS mutation={variant} checks={count} {failure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
