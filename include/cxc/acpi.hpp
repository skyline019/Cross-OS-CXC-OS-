#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::acpi {

constexpr size_t maximum_ioapics = 4;
constexpr size_t maximum_isos = 16;
constexpr size_t maximum_processors = 16;

struct ProcessorDescriptor {
    uint32_t apic_id;
    uint32_t firmware_uid;
    uint8_t madt_type;
    uint8_t reserved[3];
};

struct IoApicDescriptor {
    uint32_t id;
    uint64_t address;
    uint32_t global_irq_base;
};

struct InterruptOverride {
    uint8_t bus;
    uint8_t source;
    uint32_t global_irq;
    uint16_t flags;
};

struct MadtInfo {
    uint64_t local_apic_address;
    uint32_t processor_count;
    uint32_t lapic_entries;
    uint32_t x2apic_entries;
    uint32_t ioapic_count;
    uint32_t override_count;
    ProcessorDescriptor processors[maximum_processors];
    IoApicDescriptor ioapics[maximum_ioapics];
    InterruptOverride overrides[maximum_isos];
};

// Called while firmware's initial page tables still map ACPI physical memory.
// All required data is copied; no table pointers are retained.
bool discover_madt(uint64_t rsdp_address);
bool discovered();
const MadtInfo& madt();

// ISA IRQs default to the same GSI unless a bus-0 override says otherwise.
uint32_t isa_irq_to_gsi(uint8_t irq, uint16_t& flags);

}  // namespace cxc::acpi
