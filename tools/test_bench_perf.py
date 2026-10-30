import struct
import unittest

from bench_perf import (dtb_timebase, parse_batch, parse_samples, qemu_command,
                        summarize, summarize_batches)


def tree(frequency=10_000_000):
    def u32(value):
        return struct.pack(">I", value)

    # Root -> cpus -> timebase-frequency -> end nodes/end tree.
    structure = (u32(1) + b"\0\0\0\0" + u32(1) + b"cpus\0\0\0\0" +
                 u32(3) + u32(4) + u32(0) + u32(frequency) +
                 u32(2) + u32(2) + u32(9))
    strings = b"timebase-frequency\0"
    off_struct, off_strings = 40, 40 + len(structure)
    return (struct.pack(">10I", 0xD00DFEED, off_strings + len(strings), off_struct,
                        off_strings, 0, 17, 16, 0, len(strings), len(structure)) +
            structure + strings)


def text():
    return ("PMFS_PERF_SAMPLE mode=roundtrip size=64 i=0 pm_ticks=10 xv_ticks=20 "
            "pm_write=8 pm_read=4 pm_flush=1 order=0\n"
            "PMFS_PERF_SAMPLE mode=roundtrip size=64 i=1 pm_ticks=30 xv_ticks=40 "
            "pm_write=12 pm_read=6 pm_flush=2 order=1\n"
            "PMFS_PERF_PASS mode=roundtrip size=64 samples=2 warmup=4\n")


class TopologyTests(unittest.TestCase):
    def test_default_virtio_and_nvme_nvdimm_are_distinct(self):
        default = qemu_command("qemu", "kernel", "root.img")
        self.assertIn("virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0", default)
        self.assertNotIn("virt,nvdimm=on", default)
        combined = qemu_command("qemu", "kernel", "root.img", "nvme", "pmem.img")
        self.assertIn("virt,nvdimm=on", combined)
        self.assertIn("nvme,id=nvme0,serial=xv6nvme,drive=x0", combined)
        self.assertIn("nvdimm,id=nvdimm0,memdev=pmem0", combined)
        self.assertIn("memory-backend-file,id=pmem0,share=on,mem-path=pmem.img,size=32M",
                      combined)
        self.assertNotIn("virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0", combined)
        with self.assertRaises(ValueError):
            qemu_command("qemu", "kernel", "root.img", "unknown")


class TimebaseTests(unittest.TestCase):
    def test_roundtrip(self):
        self.assertEqual(dtb_timebase(tree()), 10_000_000)

    def test_rejects_truncated_and_invalid(self):
        for value in (tree()[:-1], b"bad", tree(0)):
            with self.assertRaises(ValueError):
                dtb_timebase(value)


class SampleTests(unittest.TestCase):
    def test_parse_exact_counts_and_metrics(self):
        rows = parse_samples(text(), "roundtrip", 64, 2)
        self.assertEqual([r["pm_ticks"] for r in rows], [10, 30])
        self.assertEqual(rows[1]["pm_write_bytes"], 12)

    def test_rejects_missing_duplicate_and_guest_failure(self):
        with self.assertRaises(ValueError):
            parse_samples(text(), "roundtrip", 64, 3)
        with self.assertRaises(ValueError):
            parse_samples(text().replace("i=1", "i=0"), "roundtrip", 64, 2)
        with self.assertRaises(ValueError):
            parse_samples(text() + "PMFS_PERF_FAIL stage=transaction\n", "roundtrip", 64, 2)

    def test_throughput_uses_total_successes_and_total_time(self):
        rows = parse_samples(text(), "roundtrip", 64, 2)
        rows = [dict(row, mode="roundtrip", size=64, run=0, warmup=False) for row in rows]
        result = summarize(rows, frequency=100, repetitions=10)["roundtrip:64"]
        self.assertEqual(result["pmfs"]["service_transactions_per_guest_second"], 5)
        self.assertAlmostEqual(result["xv6"]["service_transactions_per_guest_second"], 100 / 30)
        self.assertEqual(result["pmfs_software_requests_per_transaction"]["pm_write_bytes"], 10)
        self.assertNotIn("p99_us_pooled", result["pmfs"])

    def test_batch_measures_complete_loop(self):
        line = ("PMFS_PERF_BATCH mode=roundtrip size=64 count=2 pm_ticks=50 xv_ticks=80 "
                "pm_write=20 pm_read=10 pm_flush=3 order=0\n")
        batch = parse_batch(line, "roundtrip", 64, 2, 0)
        batch.update(mode="roundtrip", size=64, run=0, warmup=False)
        result = summarize_batches([batch], frequency=100, repetitions=10)["roundtrip:64"]
        self.assertEqual(result["pmfs"]["transactions_per_guest_second"], 4)
        with self.assertRaises(ValueError):
            parse_batch(line, "roundtrip", 64, 2, 1)


if __name__ == "__main__":
    unittest.main()
