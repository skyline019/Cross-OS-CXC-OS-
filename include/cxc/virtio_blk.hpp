#pragma once

#include <stdint.h>

namespace cxc::virtio_blk {

// M75 large-stride probe: PCI virtio-blk, read LBA 0, verify magic, OOB edge.
struct ProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t found;
    uint32_t features_ok;
    uint32_t queue_ok;
    uint32_t read_ok;
    uint32_t magic_ok;
    uint32_t oob_rejected;
    uint32_t pass;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t capacity_sectors;
    uint64_t mmio_common;
};

bool run_probe(ProbeResult& result);
const ProbeResult& probe_result();

}  // namespace cxc::virtio_blk
