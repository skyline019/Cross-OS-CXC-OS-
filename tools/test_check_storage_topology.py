"""Host-only positive and negative checks of the QMP topology validator."""
import copy
from pathlib import Path
import unittest

from check_storage_topology import NVDIMM_BASE, NVDIMM_SIZE, verify_topology


class StorageTopologyTests(unittest.TestCase):
    def setUp(self):
        self.nvme = Path("/tmp/nvme-backend.img")
        self.nvdimm = Path("/tmp/nvdimm-backend.img")
        self.pci = [{"devices": [{"qdev_id": "nvme0", "bus": 0,
                                  "class_info": {"class": 0x0108},
                                  "regions": [{"bar": 0, "type": "memory",
                                               "mem_type_64": True, "size": 16384}]}]}]
        self.memory = [{"type": "nvdimm", "data": {"id": "pmem0", "addr": NVDIMM_BASE,
                                                    "size": NVDIMM_SIZE,
                                                    "memdev": "/objects/nvd"}}]
        self.summary = {"base-memory": 128 * 1024 * 1024}
        self.blocks = [{"device": "x0", "inserted": {"file": str(self.nvme)}}]

    def check(self, pci=None, memory=None, summary=None, blocks=None, actual=None):
        return verify_topology(self.pci if pci is None else pci,
                               self.memory if memory is None else memory,
                               self.summary if summary is None else summary,
                               self.blocks if blocks is None else blocks,
                               self.nvme, self.nvdimm,
                               self.nvdimm if actual is None else actual)

    def test_positive(self):
        result = self.check()
        self.assertEqual(result["nvme"]["qdev_id"], "nvme0")
        self.assertEqual(result["nvdimm"]["data"]["addr"], NVDIMM_BASE)

    def test_missing_duplicate_and_wrong_class_fail(self):
        with self.assertRaises(ValueError):
            self.check(pci=[])
        duplicate = copy.deepcopy(self.pci)
        duplicate[0]["devices"].append(copy.deepcopy(duplicate[0]["devices"][0]))
        with self.assertRaises(ValueError):
            self.check(pci=duplicate)
        wrong = copy.deepcopy(self.pci)
        wrong[0]["devices"][0]["class_info"]["class"] = 0x0100
        with self.assertRaises(ValueError):
            self.check(pci=wrong)

    def test_wrong_bar_nvdimm_or_backing_fails(self):
        bad_bar = copy.deepcopy(self.pci)
        bad_bar[0]["devices"][0]["regions"][0]["mem_type_64"] = False
        with self.assertRaises(ValueError):
            self.check(pci=bad_bar)
        bad_memory = copy.deepcopy(self.memory)
        bad_memory[0]["data"]["addr"] += 4096
        with self.assertRaises(ValueError):
            self.check(memory=bad_memory)
        with self.assertRaises(ValueError):
            self.check(actual=self.nvme)
        with self.assertRaises(ValueError):
            self.check(blocks=[])

    def test_backends_must_be_distinct(self):
        with self.assertRaises(ValueError):
            verify_topology(self.pci, self.memory, self.summary, self.blocks,
                            self.nvme, self.nvme, self.nvme)


if __name__ == "__main__":
    unittest.main()
