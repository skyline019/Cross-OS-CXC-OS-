import struct
import tempfile
from pathlib import Path
import unittest

from check_directcore_store import debug_addresses, elf_bytes_at, source_line, store_kind


class DirectCoreIsaAuditTests(unittest.TestCase):
    def test_rv64_store_encodings_and_negative_controls(self):
        self.assertEqual(store_kind(bytes.fromhex("23300500")), "sd")
        self.assertEqual(store_kind(bytes.fromhex("0ce1")), "c.sd")
        self.assertEqual(store_kind(bytes.fromhex("2ce2")), "c.sd")
        self.assertIsNone(store_kind(bytes.fromhex("23200500")))  # sw, 32 bits
        self.assertIsNone(store_kind(bytes.fromhex("03b00500")))  # ld, not a store
        self.assertIsNone(store_kind(bytes.fromhex("00c0")))      # c.sw, 32 bits
        self.assertIsNone(store_kind(bytes.fromhex("02e0")))      # c.sdsp, not a register base
        with self.assertRaises(ValueError):
            store_kind(b"\x23")
        with self.assertRaises(ValueError):
            store_kind(b"\x23\x30")

    def test_source_and_line_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "directcore.c"
            source.write_text("a\n  *p = word;\nb\n")
            self.assertEqual(source_line(source), 2)
            source.write_text("*p = word;\n*p = word;\n")
            with self.assertRaises(ValueError):
                source_line(source)
        table = "directcore.c  2  0x1000  x\n kernel/directcore.c 2 0x1010\nother.c 2 0x1020\n"
        self.assertEqual(debug_addresses(table, 2), [0x1000, 0x1010])
        self.assertEqual(debug_addresses(table, 3), [])

    def test_bounds_checked_elf_load(self):
        image = bytearray(160)
        image[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", image, 18, 243)
        struct.pack_into("<Q", image, 32, 64)
        struct.pack_into("<HH", image, 54, 56, 1)
        struct.pack_into("<IIQQQQQQ", image, 64, 1, 5, 128, 0x8000, 0, 8, 8, 4096)
        image[128:132] = bytes.fromhex("23300500")
        self.assertEqual(elf_bytes_at(image, 0x8000), bytes.fromhex("23300500"))
        with self.assertRaises(ValueError):
            elf_bytes_at(image, 0x9000)
        with self.assertRaises(ValueError):
            elf_bytes_at(image, 0x8006)
        image[0] = 0
        with self.assertRaises(ValueError):
            elf_bytes_at(image, 0x8000)


if __name__ == "__main__":
    unittest.main()
