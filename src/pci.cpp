#include <cxc/pci.hpp>

#include <cxc/cpu.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::pci {
namespace {

Device found_devices[maximum_devices];
size_t found_count = 0;
size_t total_count = 0;
uint32_t msi_count = 0;
uint32_t msix_count = 0;
uint32_t parse_errors = 0;
bool overflow = false;

void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

uint32_t read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus) << 16) |
                             (static_cast<uint32_t>(device) << 11) |
                             (static_cast<uint32_t>(function) << 8) |
                             (offset & 0xfc);
    outl(0xcf8, address);
    return inl(0xcfc);
}

void write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
             uint32_t value) {
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus) << 16) |
                             (static_cast<uint32_t>(device) << 11) |
                             (static_cast<uint32_t>(function) << 8) |
                             (offset & 0xfc);
    outl(0xcf8, address);
    outl(0xcfc, value);
}

void write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
             uint16_t value) {
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus) << 16) |
                             (static_cast<uint32_t>(device) << 11) |
                             (static_cast<uint32_t>(function) << 8) |
                             (offset & 0xfc);
    outl(0xcf8, address);
    // Do not RMW the adjacent dword: the neighboring PCI field may be RW1C.
    outw(0xcfc + (offset & 2), value);
}

uint16_t read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t value = read32(bus, device, function, offset);
    return static_cast<uint16_t>(value >> ((offset & 2) * 8));
}

uint8_t read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t value = read32(bus, device, function, offset);
    return static_cast<uint8_t>(value >> ((offset & 3) * 8));
}

void walk_capabilities(Device& info) {
    const uint8_t header = info.header_type & 0x7f;
    if (header > 1 ||
        (read16(info.bus, info.device, info.function, 0x06) & 0x10) == 0) {
        return;
    }

    uint8_t pointer = read8(info.bus, info.device, info.function, 0x34) & 0xfc;
    for (uint32_t steps = 0; pointer != 0 && steps < 48; ++steps) {
        if (pointer < 0x40) {
            ++parse_errors;
            return;
        }
        const uint8_t id = read8(info.bus, info.device, info.function, pointer);
        const uint8_t next =
            read8(info.bus, info.device, info.function, pointer + 1) & 0xfc;
        if (id == 0 || next == pointer) {
            ++parse_errors;
            return;
        }

        if (id == 0x05 && info.msi_capability == 0) {
            info.msi_capability = pointer;
            info.msi_control = read16(info.bus, info.device, info.function,
                                      pointer + 2);
        } else if (id == 0x11 && info.msix_capability == 0) {
            if (pointer > 0xf4) {
                ++parse_errors;  // The 12-byte MSI-X capability would wrap.
                return;
            }
            info.msix_capability = pointer;
            info.msix_control = read16(info.bus, info.device, info.function,
                                       pointer + 2);
            const uint32_t table = read32(info.bus, info.device, info.function,
                                          pointer + 4);
            info.msix_table_bir = table & 7;
            info.msix_table_offset = table & ~uint32_t{7};
            const uint32_t pba = read32(info.bus, info.device, info.function,
                                        pointer + 8);
            info.msix_pba_bir = pba & 7;
            info.msix_pba_offset = pba & ~uint32_t{7};
            info.msix_table_entries = (info.msix_control & 0x07ff) + 1;
        }
        pointer = next;
    }
    if (pointer != 0) {
        ++parse_errors;
    }
}

void add_function(uint8_t bus, uint8_t device, uint8_t function) {
    const uint32_t identity = read32(bus, device, function, 0);
    Device info{};
    info.bus = bus;
    info.device = device;
    info.function = function;
    info.vendor_id = static_cast<uint16_t>(identity);
    info.device_id = static_cast<uint16_t>(identity >> 16);
    const uint32_t class_revision = read32(bus, device, function, 0x08);
    info.class_code = static_cast<uint8_t>(class_revision >> 24);
    info.subclass = static_cast<uint8_t>(class_revision >> 16);
    info.programming_interface = static_cast<uint8_t>(class_revision >> 8);
    info.header_type = read8(bus, device, function, 0x0e);
    walk_capabilities(info);

    if (info.msi_capability != 0) {
        ++msi_count;
    }
    if (info.msix_capability != 0) {
        ++msix_count;
    }
    ++total_count;
    if (found_count < maximum_devices) {
        found_devices[found_count++] = info;
    } else {
        overflow = true;
    }
}

}  // namespace

uint16_t config_read16(const Device& device, uint8_t offset) {
    return read16(device.bus, device.device, device.function, offset);
}

uint32_t config_read32(const Device& device, uint8_t offset) {
    return read32(device.bus, device.device, device.function, offset);
}

void config_write16(const Device& device, uint8_t offset, uint16_t value) {
    write16(device.bus, device.device, device.function, offset, value);
}

void config_write32(const Device& device, uint8_t offset, uint32_t value) {
    write32(device.bus, device.device, device.function, offset, value);
}

uint32_t read_bar(const Device& device, uint8_t index) {
    return index < 6 ? read32(device.bus, device.device, device.function,
                             0x10 + index * 4)
                     : 0;
}

uint8_t find_capability(const Device& device, uint8_t id) {
    const uint8_t header = device.header_type & 0x7f;
    if (header > 1 || (config_read16(device, 0x06) & 0x10) == 0) {
        return 0;
    }
    const auto read_byte = [&device](uint8_t offset) {
        return static_cast<uint8_t>(config_read32(device, offset) >>
                                    ((offset & 3) * 8));
    };
    uint8_t pointer = read_byte(0x34) & 0xfc;
    for (uint32_t steps = 0; pointer != 0 && steps < 48; ++steps) {
        if (pointer < 0x40) {
            return 0;
        }
        const uint8_t current_id = read_byte(pointer);
        const uint8_t next = read_byte(pointer + 1) & 0xfc;
        if (current_id == 0 || next == pointer) {
            return 0;
        }
        if (current_id == id) {
            return pointer;
        }
        pointer = next;
    }
    return 0;
}

bool enumerate() {
    if (cpu::interrupts_enabled()) {
        return false;
    }
    found_count = 0;
    total_count = 0;
    msi_count = 0;
    msix_count = 0;
    parse_errors = 0;
    overflow = false;

    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            if (read16(static_cast<uint8_t>(bus), device, 0, 0) == 0xffff) {
                continue;
            }
            const uint8_t header = read8(static_cast<uint8_t>(bus), device, 0, 0x0e);
            const uint8_t functions = (header & 0x80) != 0 ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                if (read16(static_cast<uint8_t>(bus), device, function, 0) !=
                    0xffff) {
                    add_function(static_cast<uint8_t>(bus), device, function);
                }
            }
        }
    }
    return total_count != 0 && !overflow && parse_errors == 0;
}

size_t device_count() {
    return found_count;
}

const Device* devices() {
    return found_devices;
}

size_t class_count(uint8_t class_code) {
    size_t count = 0;
    for (size_t index = 0; index < found_count; ++index) {
        if (found_devices[index].class_code == class_code) {
            ++count;
        }
    }
    return count;
}

uint32_t msi_capable_count() {
    return msi_count;
}

uint32_t msix_capable_count() {
    return msix_count;
}

uint32_t capability_errors() {
    return parse_errors;
}

}  // namespace cxc::pci
