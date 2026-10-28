#!/usr/bin/env python3
"""Bounded specification model for a COW namespace-root publication.

This is a model, not an instruction trace or a proof of the C implementation.
It assumes atomic checkpoint commit words, valid checksums, and correct flush
and drain semantics. At every action, pending stores may have persisted in any
subset before a crash; the selected namespace must be exactly old or new.
"""

from itertools import combinations

OLD = frozenset({("/r/old", 2), ("/r/old/sub", 3),
                 ("/r/old/sub/item", 4), ("/r/guard", 5)})
NEW = frozenset({("/r/new", 2), ("/r/new/sub", 3),
                 ("/r/new/sub/item", 4), ("/r/guard", 5)})


def subsets(items):
    items = sorted(items)
    for n in range(len(items) + 1):
        for selection in combinations(items, n):
            yield set(selection)


def recover(durable, new_namespace=NEW):
    """Return the chosen namespace, or None if a committed root is incomplete."""
    if "word" not in durable:
        return OLD
    if not {"header", "inode", "dentry"} <= durable:
        return None
    return new_namespace


def explore(actions, new_namespace=NEW, initially_durable=()):
    """Enumerate early evictions before and after every abstract action."""
    durable, pending, checked, outcomes = set(initially_durable), set(), 0, set()
    for position in range(len(actions) + 1):
        for evicted in subsets(pending):
            image = durable | evicted
            result = recover(image, new_namespace)
            checked += 1
            if result not in (OLD, NEW):
                return checked, outcomes, (position, sorted(image), result)
            outcomes.add("old" if result == OLD else "new")
        if position == len(actions):
            break
        operation, keys = actions[position]
        if operation == "store":
            pending.update(keys)
        elif operation == "drain":
            durable.update(pending.intersection(keys))
            pending.difference_update(keys)
        else:
            raise ValueError(operation)
    return checked, outcomes, None


def main():
    # New pages, inactive slot header, and final atomic commit word. Drains are
    # range-specific: an unrelated unflushed store may or may not persist.
    correct = [
        ("store", {"inode"}), ("store", {"dentry"}),
        ("drain", {"inode", "dentry"}),
        ("store", {"header"}), ("drain", {"header"}),
        ("store", {"word"}), ("drain", {"word"}),
    ]
    checked, outcomes, bad = explore(correct)
    if bad is not None or outcomes != {"old", "new"}:
        raise SystemExit(f"VFS_MODEL_FAIL protocol={bad} outcomes={outcomes}")
    print(f"VFS_MODEL_PASS protocol=cow-rename crash_states={checked} "
          "evictions=all_subsets outcomes=old,new")

    # A non-replacing rename reuses the already durable, immutable inode
    # snapshot and publishes only a new dentry page.
    single_page = [("store", {"dentry"}), ("drain", {"dentry"}),
                   ("store", {"header"}), ("drain", {"header"}),
                   ("store", {"word"}), ("drain", {"word"})]
    checked, outcomes, bad = explore(single_page, initially_durable={"inode"})
    if bad is not None or outcomes != {"old", "new"}:
        raise SystemExit(f"VFS_MODEL_FAIL reuse-inode={bad} outcomes={outcomes}")
    print(f"VFS_MODEL_PASS protocol=reuse-inode crash_states={checked} "
          "evictions=all_subsets outcomes=old,new")
    early_single = [("store", {"dentry"}), ("store", {"header"}),
                    ("drain", {"header"}), ("store", {"word"}),
                    ("drain", {"word"}), ("drain", {"dentry"})]
    _, _, bad = explore(early_single, initially_durable={"inode"})
    if bad is None:
        raise SystemExit("VFS_MODEL_FAIL reuse-inode early-publish was not detected")
    print(f"VFS_MODEL_NEG_PASS mutation=reuse-early counterexample={bad[:2]}")

    # Publish the checkpoint before draining the new metadata pages.
    early = [
        ("store", {"inode"}), ("store", {"dentry"}),
        ("store", {"header"}), ("drain", {"header"}),
        ("store", {"word"}), ("drain", {"word"}),
        ("drain", {"inode", "dentry"}),
    ]
    _, _, bad = explore(early)
    if bad is None:
        raise SystemExit("VFS_MODEL_FAIL early-publish was not detected")
    print(f"VFS_MODEL_NEG_PASS mutation=early-publish counterexample={bad[:2]}")

    # Publishing a changed parent without rewriting descendant paths creates
    # a namespace that is neither complete old nor complete new.
    partial = frozenset({("/r/new", 2), ("/r/old/sub", 3),
                         ("/r/old/sub/item", 4), ("/r/guard", 5)})
    _, _, bad = explore(correct, partial)
    if bad is None:
        raise SystemExit("VFS_MODEL_FAIL partial-subtree was not detected")
    print(f"VFS_MODEL_NEG_PASS mutation=partial-subtree counterexample={bad[:2]}")

    # Mutating live metadata in place exposes all subsets of entry updates.
    old_entries, new_entries = sorted(OLD), sorted(NEW)
    # Match entries by inode, rather than lexical path ordering.
    new_by_ino = {ino: path for path, ino in new_entries}
    inodes = sorted(ino for _, ino in old_entries if ino != 5)
    old_by_ino = {ino: path for path, ino in old_entries}
    counterexample = None
    for evicted in subsets(inodes):
        state = frozenset((new_by_ino[ino] if ino in evicted else path, ino)
                          for ino, path in old_by_ino.items())
        if state not in (OLD, NEW):
            counterexample = sorted(evicted)
            break
    if counterexample is None:
        raise SystemExit("VFS_MODEL_FAIL in-place update was not detected")
    print(f"VFS_MODEL_NEG_PASS mutation=in-place counterexample={counterexample}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
