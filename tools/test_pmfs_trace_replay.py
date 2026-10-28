import unittest

from pmfs_trace_replay import Model, apply_all_stores, candidates, parse_trace


TRACE = """PMTRACE BEGIN version=1 line=64 bytes=16384
PMTRACE W seq=0 off=8192 n=4 atomic=0 data=01020304
PMTRACE F seq=1 off=8192
PMTRACE D seq=2
PMTRACE W seq=3 off=8 n=8 atomic=1 data=0000000000000000
PMTRACE F seq=4 off=0
PMTRACE D seq=5
PMTRACE W seq=6 off=8 n=8 atomic=1 data=0102030405060708
PMTRACE F seq=7 off=0
PMTRACE D seq=8
PMTRACE ACK seq=9
PMTRACE END seq=10
"""


class TraceTests(unittest.TestCase):
    def test_parse_and_reconstruct(self):
        size, events = parse_trace(TRACE)
        final = apply_all_stores(bytes(size), events)
        self.assertEqual(final[8192:8196], b"\x01\x02\x03\x04")
        self.assertEqual(final[8:16], bytes(range(1, 9)))

    def test_rejects_gaps_and_bad_ranges(self):
        for text in (TRACE.replace("F seq=1", "F seq=10"),
                     TRACE.replace("off=8192 n=4", "off=8254 n=4"),
                     TRACE.replace("PMTRACE ACK seq=9\n", "")):
            with self.assertRaises(ValueError):
                parse_trace(text)

    def test_partial_store_candidates_and_ack(self):
        size, events = parse_trace(TRACE)
        result = candidates(bytes(size), events)
        self.assertTrue(any(any(c["scenario"].startswith("partial-") for c in case["contexts"])
                            for case in result))
        post_ack = [case for case in result if case["requires_new"]]
        self.assertTrue(post_ack)
        self.assertTrue(all(case["anchor"] for case in post_ack))

    def test_omitted_flush_loses_data_even_when_commit_persists(self):
        size, events = parse_trace(TRACE)
        model = Model(bytes(size), omit_flush_page=2)
        for event in events:
            model.step(event)
        stable = model.snapshot()
        self.assertNotIn(8192, stable)
        self.assertEqual(stable[0][8:16], bytes(range(1, 9)))

    def test_eager_eviction_can_persist_unflushed_data(self):
        size, events = parse_trace(TRACE)
        model = Model(bytes(size), history="eager")
        model.step(events[0])
        self.assertEqual(model.snapshot()[8192][:4], b"\x01\x02\x03\x04")


if __name__ == "__main__":
    unittest.main()
