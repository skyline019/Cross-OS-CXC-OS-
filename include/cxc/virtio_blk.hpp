#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::virtio_blk {

constexpr uint32_t sector_size = 512;

// M75–M76: modern virtio-blk. Device stays online after successful init (A2 path).
struct ProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t found;
    uint32_t features_ok;
    uint32_t queue_ok;
    uint32_t read_ok;
    uint32_t magic_ok;
    uint32_t oob_rejected;
    uint32_t write_ok;
    uint32_t rw_roundtrip_ok;
    uint32_t oob_write_rejected;
    uint32_t pass;
    uint32_t rw_pass;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t capacity_sectors;
    uint64_t mmio_common;
};

// Bring-up: discover, map, features, queue, LBA0 magic + OOB read edge.
// On success the device remains online for read_sectors/write_sectors.
bool run_probe(ProbeResult& result);

// M76: write LBA1 pattern, read back, OOB write edge; requires online device.
bool run_rw_probe(ProbeResult& result);

const ProbeResult& probe_result();

bool is_ready();
uint64_t capacity_sectors();

// Synchronous sector I/O. buffer length must be count * sector_size.
// Returns false on OOB, not-ready, or device error (sets last failure).
bool read_sectors(uint64_t lba, void* buffer, size_t count);
bool write_sectors(uint64_t lba, const void* buffer, size_t count);

uint32_t last_io_failure();

}  // namespace cxc::virtio_blk
