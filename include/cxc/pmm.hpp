#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cxc/uefi.hpp>

namespace cxc::pmm {

constexpr uint64_t page_size = 4096;

struct Region {
    uint64_t base;
    uint64_t end;
};

// Milestone 1 uses an inline-extent allocator. Metadata lives in the first
// page of each free extent and links physical (not virtual) addresses.
bool initialize(const uefi::BootMemoryMap& map);

// Must be called only after direct_map_offset is active in the CPU page tables.
void set_direct_map_offset(uint64_t direct_map_offset);
void* physical_to_virtual(uint64_t physical_address);

// Any contiguous page count is supported without power-of-two rounding.
uint64_t allocate_pages(uint64_t count);

// Allocates wholly below the exclusive physical limit. This is required for
// devices with a narrow DMA mask; the limit need not coincide with an extent.
uint64_t allocate_pages_below(uint64_t count, uint64_t limit);
void free_pages(uint64_t physical_address, uint64_t count);

uint64_t total_pages();
uint64_t free_pages_count();
uint64_t extent_count();
size_t region_count();
const Region& region(size_t index);

}  // namespace cxc::pmm
