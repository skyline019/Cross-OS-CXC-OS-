"""Host-only tests for the QEMU NVMe command and log-order audit."""
from types import SimpleNamespace
import unittest

from nvme_trace_audit import audit_journal, parse_trace


class TraceAuditTests(unittest.TestCase):
    def setUp(self):
        self.layout = SimpleNamespace(start=2, nlog=31, size=2000)
        # Empty-log recovery, followed by one two-block transaction.
        self.tokens = [("F", None), ("H", 2), ("F", None), ("D", 3), ("D", 4),
                       ("F", None), ("H", 2), ("F", None), ("O", 50), ("O", 51),
                       ("F", None), ("H", 2), ("F", None)]

    def trace(self, tokens):
        lines = []
        for cid, (kind, block) in enumerate(tokens):
            opc = 0 if kind == "F" else 1
            lines.append(f"pci_nvme_io_cmd cid {cid} nsid 0x1 sqid 1 opc 0x{opc:x} opname 'test'")
            if kind == "F":
                lines += ["pci_nvme_flush_ns nsid 0x1", f"pci_nvme_misc_cb cid {cid}"]
            else:
                lines += [f"pci_nvme_write cid {cid} opname 'test' nsid 1 nlb 2 count 1024 lba 0x{block * 2:x}",
                          f"pci_nvme_rw_cb cid {cid} blk 'x0'"]
        return "\n".join(lines) + "\n"

    def test_complete_trace_and_order(self):
        tokens, count = parse_trace(self.trace(self.tokens), self.layout)
        self.assertEqual(tokens, self.tokens)
        self.assertEqual(count, len(self.tokens))
        self.assertEqual(audit_journal(tokens, self.layout), 1)

    def test_missing_barrier_and_bad_home_rejected(self):
        for mutated in (self.tokens[:5] + self.tokens[6:],
                        self.tokens[:9] + [("O", 50)] + self.tokens[10:],
                        self.tokens[:-1]):
            with self.subTest(mutated=mutated):
                with self.assertRaises(ValueError):
                    audit_journal(mutated, self.layout)

    def test_mismatched_completion_or_missing_flush_callback_rejected(self):
        trace = self.trace(self.tokens)
        with self.assertRaises(ValueError):
            parse_trace(trace.replace("pci_nvme_misc_cb cid 0", "pci_nvme_misc_cb cid 99", 1),
                        self.layout)
        with self.assertRaises(ValueError):
            parse_trace(trace.replace("pci_nvme_flush_ns nsid 0x1\n", "", 1), self.layout)
        with self.assertRaises(ValueError):
            parse_trace(trace.replace("nlb 2 count 1024", "nlb 1 count 512", 1), self.layout)


if __name__ == "__main__":
    unittest.main()
