"""Host-only checks for the xv6 recovery oracle's disk and trace validation."""

from pathlib import Path
import shutil
import struct
import tempfile
import unittest

from xv6_log_recovery import (BSIZE, DiskLog, FAILURE_LINE, FSMAGIC, SHELL_PROMPT,
                              TestFailure, check_recovery_trace, disk_arguments)


class DiskArgumentsTests(unittest.TestCase):
    def test_backend_selection_is_explicit_and_writeback_is_not_noflush(self):
        for backend, device in (("virtio", "virtio-blk-device"), ("nvme", "nvme,")):
            with self.subTest(backend=backend):
                args = disk_arguments(Path("/tmp/test.img"), backend)
                self.assertEqual(args[0], "-drive")
                self.assertIn("cache=writeback", args[1])
                self.assertNotIn("no-flush", args[1])
                self.assertEqual(args[2], "-device")
                self.assertTrue(args[3].startswith(device))
        with self.assertRaises(ValueError):
            disk_arguments(Path("/tmp/test.img"), "unknown")


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
    def test_serial_failure_requires_a_complete_line(self):
        self.assertEqual(FAILURE_LINE.findall(b"XV6_LOG_TEST_FAIL code=1"), [])
        self.assertEqual(FAILURE_LINE.findall(b"XV6_LOG_TEST_FAIL code=12\r\n"), [b"12"])

    def test_shell_prompt_must_be_at_line_start_and_not_a_command(self):
        self.assertIsNotNone(SHELL_PROMPT.search(b"init: starting sh\r\n$ "))
        self.assertIsNone(SHELL_PROMPT.search(b"init: starting sh\r\n$ command\r\n"))
        self.assertIsNone(SHELL_PROMPT.search(b"not a $ prompt\r\n"))

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
