import struct
import unittest
import zlib

from directcore_trace_replay import (LINE, MAGIC, SALT, audit_protocol,
                                     expected_pattern, first_bad, inspect,
                                     parse_trace, validate_reconstruction)
from pmfs_trace_replay import apply_all_stores, candidates


def root_word(index):
    return index | (zlib.crc32(struct.pack("<II", index, SALT)) << 32)


def node(ids, crcs=None):
    ids = list(ids) + [0] * (8 - len(ids))
    crcs = list(crcs or []) + [0] * (8 - len(crcs or []))
    return struct.pack("<8I8I", *(ids + crcs))


def inode(tree=0, tree_crc=0, size=0):
    return struct.pack("<IIQQIBBHIIQQQ", 1, tree, 1, size, 1, 1, 1, 0,
                       tree_crc, 0, 0, 0, 0)


def checkpoint(epoch, table, table_crc, units):
    cp = bytearray(struct.pack("<IHHQIIB3xI28xI", MAGIC, 1, 0, epoch,
                               table, table_crc, 1, units, 0))
    struct.pack_into("<I", cp, 60, zlib.crc32(cp))
    return bytes(cp)


def fixture():
    units = 32
    image = bytearray(units * LINE)

    def put(index, contents):
        image[index * LINE:(index + 1) * LINE] = contents

    empty = inode()
    empty_table = node([0, 2], [0, zlib.crc32(empty)])
    put(2, empty)
    put(3, empty_table)
    put(4, checkpoint(1, 3, zlib.crc32(empty_table), units))
    old_data = expected_pattern(1, 128)
    put(5, old_data[:64])
    put(6, old_data[64:])
    old_node = node([5, 6])
    old_inode = inode(7, zlib.crc32(old_node), 128)
    old_table = node([0, 8], [0, zlib.crc32(old_inode)])
    put(7, old_node)
    put(8, old_inode)
    put(9, old_table)
    put(10, checkpoint(2, 9, zlib.crc32(old_table), units))
    struct.pack_into("<Q", image, 0, root_word(4))
    struct.pack_into("<Q", image, LINE, root_word(10))

    new_data = expected_pattern(2, 128)
    new_node = node([11, 12])
    new_inode = inode(13, zlib.crc32(new_node), 128)
    new_table = node([0, 14], [0, zlib.crc32(new_inode)])
    objects = [(11, new_data[:64]), (12, new_data[64:]), (13, new_node),
               (14, new_inode), (15, new_table),
               (16, checkpoint(3, 15, zlib.crc32(new_table), units))]
    events = []
    for index, value in objects:
        events.append({"kind": "W", "seq": len(events), "off": index * LINE,
                       "data": value, "atomic": False})
    events.append({"kind": "O", "seq": len(events)})
    for index, _ in objects:
        events.append({"kind": "F", "seq": len(events), "off": index * LINE})
    events.append({"kind": "D", "seq": len(events)})
    events.append({"kind": "W", "seq": len(events), "off": 0,
                   "data": struct.pack("<Q", root_word(16)), "atomic": True})
    events.append({"kind": "O", "seq": len(events)})
    events.append({"kind": "F", "seq": len(events), "off": 0})
    for kind in ("D", "ACK", "END"):
        events.append({"kind": kind, "seq": len(events)})
    return bytes(image), events


def trace_text(base, events):
    lines = [f"PMTRACE BEGIN version=1 line=64 bytes={len(base)}"]
    for event in events:
        kind, seq = event["kind"], event["seq"]
        if kind == "W":
            data = event["data"]
            lines.append(f"PMTRACE W seq={seq} off={event['off']} n={len(data)} "
                         f"atomic={int(event['atomic'])} data={data.hex()}")
        elif kind == "F":
            lines.append(f"PMTRACE F seq={seq} off={event['off']}")
        else:
            lines.append(f"PMTRACE {kind} seq={seq}")
    return "\n".join(lines) + "\n"


class TraceReplayTests(unittest.TestCase):
    def test_realistic_protocol_and_replay_candidates(self):
        base, events = fixture()
        parsed = parse_trace(trace_text(base, events), len(base))
        self.assertEqual(parsed, events)
        self.assertEqual(audit_protocol(parsed)["drain_groups"], 2)
        self.assertEqual(audit_protocol(parsed)["ordering_fences"], 2)
        self.assertEqual(inspect(base, {})[0], "old")
        cases = candidates(base, events)
        self.assertGreater(len(cases), 1)
        for case in cases:
            state, _ = inspect(base, case["patch"])
            if case["requires_new"]:
                self.assertEqual(state, "new")

    def test_missing_flush_and_early_root_have_counterexamples(self):
        base, events = fixture()
        dropped = [e for e in events if not (e["kind"] == "F" and e["off"] == 11 * LINE)]
        with self.assertRaises(ValueError):
            audit_protocol(dropped)
        no_order = [e for e in events if e["kind"] != "O"]
        with self.assertRaises(ValueError):
            audit_protocol(no_order)
        first_bad(base, dropped)
        early = list(events)
        index = next(i for i, e in enumerate(early) if e["kind"] == "W" and e["atomic"])
        early.insert(0, early.pop(index))
        first_bad(base, early)

    def test_full_image_reconstruction_rejects_untraced_write(self):
        base, events = fixture()
        prefix = bytes(64)
        observed = prefix + apply_all_stores(base, events)
        validate_reconstruction(prefix + base, observed, events, reserved=64)
        changed = bytearray(observed)
        changed[64 + 30 * LINE] ^= 1
        with self.assertRaises(ValueError):
            validate_reconstruction(prefix + base, bytes(changed), events, reserved=64)
        changed = bytearray(observed)
        changed[0] ^= 1
        with self.assertRaises(ValueError):
            validate_reconstruction(prefix + base, bytes(changed), events, reserved=64)

    def test_corruption_and_incomplete_trace_rejected(self):
        base, events = fixture()
        with self.assertRaises(ValueError):
            inspect(base, {0: bytes(64)})
        with self.assertRaises(ValueError):
            inspect(base, {5 * LINE: bytes(64)})
        with self.assertRaises(ValueError):
            parse_trace(trace_text(base, events[:-1]), len(base))
        with self.assertRaises(ValueError):
            parse_trace(trace_text(base, events).replace("seq=0 ", "seq=4 ", 1), len(base))


if __name__ == "__main__":
    unittest.main()
