#include <cxc/acpi.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::acpi {
namespace {

constexpr uint32_t maximum_table_size = 1024 * 1024;
MadtInfo cached_madt;
bool valid = false;

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t read32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read64(const uint8_t* bytes) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

bool signature_is(const uint8_t* table, const char* expected) {
    for (size_t index = 0; index < 4; ++index) {
        if (table[index] != static_cast<uint8_t>(expected[index])) {
            return false;
        }
    }
    return true;
}

bool checksum_is_valid(const uint8_t* bytes, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t index = 0; index < length; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    return sum == 0;
}

bool table_header_is_valid(const uint8_t* table, const char* signature,
                           uint32_t& length) {
    if (table == nullptr || !signature_is(table, signature)) {
        return false;
    }
    length = read32(table + 4);
    return length >= 36 && length <= maximum_table_size &&
           checksum_is_valid(table, length);
}

bool record_processor(MadtInfo& parsed, uint32_t apic_id,
                      uint32_t firmware_uid, uint8_t madt_type,
                      uint32_t flags) {
    // This milestone starts only processors that firmware marks enabled.
    // Online-capable but currently disabled entries require a separate ACPI
    // device-enable protocol and are deliberately not started with SIPI.
    if ((flags & 1) == 0) {
        return true;
    }
    for (uint32_t index = 0; index < parsed.processor_count; ++index) {
        if (parsed.processors[index].apic_id == apic_id) {
            return false;
        }
    }
    if (parsed.processor_count >= maximum_processors) {
        return false;
    }
    ProcessorDescriptor& descriptor =
        parsed.processors[parsed.processor_count++];
    descriptor.apic_id = apic_id;
    descriptor.firmware_uid = firmware_uid;
    descriptor.madt_type = madt_type;
    return true;
}

bool parse_madt(const uint8_t* table, uint32_t length) {
    if (length < 44) {
        return false;
    }
    MadtInfo parsed{};
    parsed.local_apic_address = read32(table + 36);

    uint32_t offset = 44;
    while (offset < length) {
        if (length - offset < 2) {
            return false;
        }
        const uint8_t type = table[offset];
        const uint8_t entry_length = table[offset + 1];
        if (entry_length < 2 || entry_length > length - offset) {
            return false;
        }
        const uint8_t* entry = table + offset;

        if (type == 0 && entry_length >= 8) {
            ++parsed.lapic_entries;
            if (!record_processor(parsed, entry[3], entry[2], type,
                                  read32(entry + 4))) {
                return false;
            }
        } else if (type == 1 && entry_length >= 12) {
            if (parsed.ioapic_count >= maximum_ioapics) {
                return false;
            }
            IoApicDescriptor& ioapic = parsed.ioapics[parsed.ioapic_count++];
            ioapic.id = entry[2];
            ioapic.address = read32(entry + 4);
            ioapic.global_irq_base = read32(entry + 8);
        } else if (type == 2 && entry_length >= 10) {
            if (parsed.override_count >= maximum_isos) {
                return false;
            }
            InterruptOverride& override = parsed.overrides[parsed.override_count++];
            override.bus = entry[2];
            override.source = entry[3];
            override.global_irq = read32(entry + 4);
            override.flags = read16(entry + 8);
        } else if (type == 5 && entry_length >= 12) {
            parsed.local_apic_address = read64(entry + 4);
        } else if (type == 9 && entry_length >= 16) {
            ++parsed.x2apic_entries;
            if (!record_processor(parsed, read32(entry + 2), read32(entry + 4),
                                  type, read32(entry + 8))) {
                return false;
            }
        }
        offset = static_cast<uint32_t>(offset + entry_length);
    }

    if (parsed.local_apic_address == 0 || parsed.processor_count == 0) {
        return false;
    }
    cached_madt = parsed;
    valid = true;
    return true;
}

bool scan_table_entries(const uint8_t* table, uint32_t length, bool xsdt) {
    const uint32_t entry_size = xsdt ? 8 : 4;
    if ((length - 36) % entry_size != 0) {
        return false;
    }
    for (uint32_t offset = 36; offset < length; offset += entry_size) {
        const uint64_t address = xsdt ? read64(table + offset)
                                      : read32(table + offset);
        if (address == 0) {
            continue;
        }
        const auto* candidate = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(address));
        if (!signature_is(candidate, "APIC")) {
            continue;
        }
        uint32_t madt_length = 0;
        if (!table_header_is_valid(candidate, "APIC", madt_length)) {
            return false;
        }
        return parse_madt(candidate, madt_length);
    }
    return false;
}

}  // namespace

bool discover_madt(uint64_t rsdp_address) {
    cached_madt = MadtInfo{};
    valid = false;
    if (rsdp_address == 0) {
        return false;
    }
    const auto* rsdp = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(rsdp_address));
    const char expected[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    for (size_t index = 0; index < 8; ++index) {
        if (rsdp[index] != static_cast<uint8_t>(expected[index])) {
            return false;
        }
    }
    if (!checksum_is_valid(rsdp, 20)) {
        return false;
    }

    const uint8_t revision = rsdp[15];
    const uint32_t rsdt_address = read32(rsdp + 16);
    uint64_t xsdt_address = 0;
    if (revision >= 2) {
        const uint32_t length = read32(rsdp + 20);
        if (length < 36 || length > 1024 || !checksum_is_valid(rsdp, length)) {
            return false;
        }
        xsdt_address = read64(rsdp + 24);
    }

    if (xsdt_address != 0) {
        const auto* xsdt = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(xsdt_address));
        uint32_t length = 0;
        return table_header_is_valid(xsdt, "XSDT", length) &&
               scan_table_entries(xsdt, length, true);
    }
    if (rsdt_address != 0) {
        const auto* rsdt = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(rsdt_address));
        uint32_t length = 0;
        return table_header_is_valid(rsdt, "RSDT", length) &&
               scan_table_entries(rsdt, length, false);
    }
    return false;
}

bool discovered() {
    return valid;
}

const MadtInfo& madt() {
    return cached_madt;
}

uint32_t isa_irq_to_gsi(uint8_t irq, uint16_t& flags) {
    if (valid) {
        for (uint32_t index = 0; index < cached_madt.override_count; ++index) {
            const InterruptOverride& entry = cached_madt.overrides[index];
            if (entry.bus == 0 && entry.source == irq) {
                flags = entry.flags;
                return entry.global_irq;
            }
        }
    }
    flags = 0;
    return irq;
}

}  // namespace cxc::acpi
