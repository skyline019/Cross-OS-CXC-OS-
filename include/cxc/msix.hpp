#pragma once

#include <stdint.h>

namespace cxc::msix {

struct TableEntry {
    uint32_t address_low;
    uint32_t address_high;
    uint32_t data;
    uint32_t vector_control;
};
static_assert(sizeof(TableEntry) == 16);

constexpr uint32_t vector_masked = 1;

// Views require the caller to map the corresponding PCI BAR as device memory.
volatile TableEntry* table_entry(volatile uint8_t* bar, uint32_t offset,
                                 uint32_t index);

// Programs a non-remapped physical-destination MSI-X entry. The entry is
// masked while its address/data change and unmasked, if requested, last.
bool program_entry(volatile TableEntry* entry, uint32_t apic_id,
                   uint8_t vector, bool masked);
void set_masked(volatile TableEntry* entry, bool masked);
bool is_masked(volatile const TableEntry* entry);

// Returns one bit from the qword-indexed Pending Bit Array.
bool pending(volatile const uint8_t* pba, uint32_t index);

}  // namespace cxc::msix
