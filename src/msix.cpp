#include <cxc/msix.hpp>

#include <stddef.h>

namespace cxc::msix {

volatile TableEntry* table_entry(volatile uint8_t* bar, uint32_t offset,
                                 uint32_t index) {
    if (bar == nullptr || (offset & 7) != 0) {
        return nullptr;
    }
    return reinterpret_cast<volatile TableEntry*>(
        bar + offset + static_cast<size_t>(index) * sizeof(TableEntry));
}

bool program_entry(volatile TableEntry* entry, uint32_t apic_id,
                   uint8_t vector, bool masked) {
    if (entry == nullptr || apic_id > 0xff || vector == 0) {
        return false;
    }

    entry->vector_control = vector_masked;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    entry->address_low = 0xfee00000u | (apic_id << 12);
    entry->address_high = 0;
    entry->data = vector;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    entry->vector_control = masked ? vector_masked : 0;
    return true;
}

void set_masked(volatile TableEntry* entry, bool masked) {
    if (entry == nullptr) {
        return;
    }
    const uint32_t old = entry->vector_control;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    entry->vector_control = masked ? (old | vector_masked)
                                   : (old & ~vector_masked);
}

bool is_masked(volatile const TableEntry* entry) {
    return entry == nullptr || (entry->vector_control & vector_masked) != 0;
}

bool pending(volatile const uint8_t* pba, uint32_t index) {
    if (pba == nullptr) {
        return false;
    }
    const auto* words = reinterpret_cast<volatile const uint64_t*>(pba);
    const uint64_t value = words[index / 64];
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return (value & (uint64_t{1} << (index % 64))) != 0;
}

}  // namespace cxc::msix
