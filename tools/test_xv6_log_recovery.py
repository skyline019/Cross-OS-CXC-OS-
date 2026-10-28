"""Host-only checks for the xv6 recovery oracle's disk and trace validation."""

from pathlib import Path
import shutil
import struct
import tempfile
import unittest

from xv6_log_recovery import BSIZE, DiskLog, FSMAGIC, TestFailure, check_recovery_trace


class DiskLogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "disk.img"
        with self.path.open("wb") as f:
            f.truncate(64 * BSIZE)
        with self.path.open("r+b") as f:
            f.seek(BSIZE)
            f.write(struct.pack("<8I", FSMAGIC, 64, 40, 30, 5, 2, 7, 8))
        self.log = DiskLog(self.path)

    def tearDown(self):
        self.temp.cleanup()

    def header(self, count, *blocks):
        with self.path.open("r+b") as f:
            f.seek(2 * BSIZE)
            f.write(struct.pack("<i", count))
            for block in blocks:
                f.write(struct.pack("<I", block))

    def block(self, block, value):
        with self.path.open("r+b") as f:
            f.seek(block * BSIZE)
            f.write(bytes([value]) * BSIZE)

    def test_committed_blocks_and_partial_install(self):
        self.header(2, 20, 21)
        self.block(3, 1)
        self.block(4, 2)
        self.block(20, 1)
        self.block(21, 9)
        self.assertEqual(self.log.header(), [20, 21])
        self.assertEqual(self.log.installed([20, 21]), [True, False])
        self.block(21, 2)
        self.assertEqual(self.log.installed([20, 21]), [True, True])

    def test_invalid_headers_are_rejected(self):
        for count, blocks in [(-1, ()), (5, ()), (2, (20, 20)),
                              (1, (2,)), (1, (64,))]:
            with self.subTest(count=count, blocks=blocks):
                self.header(count, *blocks)
                with self.assertRaises(TestFailure):
                    self.log.header()

    def test_negative_control_changes_only_the_copy(self):
        self.header(2, 20, 21)
        self.block(3, 7)
        copy = self.path.with_name("negative.img")
        shutil.copyfile(self.path, copy)
        DiskLog(copy).discard_commit_header()
        self.assertEqual(self.log.header(), [20, 21])
        self.assertEqual(DiskLog(copy).header(), [])
        self.assertEqual(copy.read_bytes()[3 * BSIZE:4 * BSIZE], bytes([7]) * BSIZE)

    def test_invalid_superblock_is_rejected(self):
        with self.path.open("r+b") as f:
            f.seek(BSIZE)
            f.write(struct.pack("<I", 0))
        with self.assertRaises(TestFailure):
            DiskLog(self.path)


class TraceTests(unittest.TestCase):
    def test_exact_replay_order_and_destinations(self):
        output = b"recovering tail 0 dst 20\r\nrecovering tail 1 dst 21\r\n"
        check_recovery_trace(output, [20, 21])
        for expected in ([21, 20], [20], []):
            with self.subTest(expected=expected):
                with self.assertRaises(TestFailure):
                    check_recovery_trace(output, expected)
        check_recovery_trace(b"no recovery\n", [])


if __name__ == "__main__":
    unittest.main()
