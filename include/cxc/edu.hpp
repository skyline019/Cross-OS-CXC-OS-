#pragma once

#include <stdint.h>

namespace cxc::edu {

struct TestInfo {
    uint64_t mmio_physical;
    uint64_t source_physical;
    uint64_t destination_physical;
    uint64_t irq_calls;
    uint32_t identification;
    uint32_t direct_interrupts;
    uint32_t dma_interrupts;
    uint32_t dma_bytes;
    uint32_t msi_address;
    uint16_t msi_data;
    uint8_t vector;
};

// Exercises QEMU's documented 1234:11e8 educational PCI device: MMIO
// liveness, a direct MSI, and a 256-byte RAM -> EDU -> RAM DMA round trip
// with a completion MSI in each direction. The device is quiesced and all
// IRQ/DMA allocations are released before returning.
bool run_msi_dma_test();
const TestInfo& test_info();

}  // namespace cxc::edu
