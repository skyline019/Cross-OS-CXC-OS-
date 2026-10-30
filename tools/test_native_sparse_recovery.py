"""Host-only negative controls for the native sparse-media oracle."""
from pathlib import Path
import struct
import tempfile
import unittest

from native_sparse_recovery import (BSIZE, DIRECT_END, INDIRECT_AT, LAST_BYTE,
                                    PATCH_AT, SparseDisk, TestFailure)
from xv6_log_recovery import FSMAGIC


class SparseDiskTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "disk.img"
        self.data = bytearray(64 * BSIZE)
        struct.pack_into("<8I", self.data, BSIZE, FSMAGIC, 64, 40, 16, 5, 2, 7, 8)
        self.inode(1, 1, 1, 48, [20] + [0] * 12)
        ptr = [0] * 13
        ptr[0], ptr[11], ptr[12] = 22, 23, 24
        self.inode(2, 2, 1, LAST_BYTE + 1, ptr)
        struct.pack_into("<H14s", self.data, 20 * BSIZE, 1, b".")
        struct.pack_into("<H14s", self.data, 20 * BSIZE + 16, 1, b"..")
        struct.pack_into("<H14s", self.data, 20 * BSIZE + 32, 2, b"ni-sparse")
        struct.pack_into("<I", self.data, 24 * BSIZE + 4 * 4, 25)
        struct.pack_into("<I", self.data, 24 * BSIZE + 255 * 4, 26)
        self.data[22 * BSIZE:22 * BSIZE + 4] = b"HEAD"
        self.data[23 * BSIZE + DIRECT_END % BSIZE] = ord("D")
        self.data[25 * BSIZE + INDIRECT_AT % BSIZE:
                  25 * BSIZE + INDIRECT_AT % BSIZE + 3] = b"IND"
        self.data[26 * BSIZE + LAST_BYTE % BSIZE] = ord("Z")
        for b in (20, 22, 23, 24, 25, 26):
            self.allocate(b)
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def inode(self, number, typ, nlink, size, pointers):
        struct.pack_into("<hhhhI13I", self.data, 7 * BSIZE + number * 64,
                         typ, 0, 0, nlink, size, *pointers)

    def allocate(self, block):
        self.data[8 * BSIZE + block // 8] |= 1 << (block % 8)

    def save(self):
        self.path.write_bytes(self.data)

    def test_valid_sparse_and_patched_media(self):
        disk = SparseDisk(self.path)
        self.assertTrue(disk.inspect(False))
        with self.assertRaises(TestFailure):
            disk.inspect(True)
        struct.pack_into("<I", self.data, 7 * BSIZE + 2 * 64 + 12 + 6 * 4, 27)
        self.allocate(27)
        at = 27 * BSIZE + PATCH_AT % BSIZE
        self.data[at:at + 3] = b"NEW"
        self.save()
        self.assertTrue(SparseDisk(self.path).inspect(True))
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)

    def test_detects_extra_hole_allocation(self):
        struct.pack_into("<I", self.data, 7 * BSIZE + 2 * 64 + 12 + 3 * 4, 27)
        self.allocate(27)
        self.save()
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)

    def test_detects_missing_bitmap_bit_and_data_corruption(self):
        self.data[8 * BSIZE + 25 // 8] &= ~(1 << (25 % 8))
        self.save()
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)
        self.allocate(25)
        self.data[25 * BSIZE + 99] = 1
        self.save()
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)

    def test_detects_aliased_blocks_and_bad_inode(self):
        struct.pack_into("<I", self.data, 24 * BSIZE + 4 * 4, 23)
        self.save()
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)
        struct.pack_into("<I", self.data, 24 * BSIZE + 4 * 4, 25)
        struct.pack_into("<I", self.data, 7 * BSIZE + 2 * 64 + 8, 7)
        self.save()
        with self.assertRaises(TestFailure):
            SparseDisk(self.path).inspect(False)


if __name__ == "__main__":
    unittest.main()
