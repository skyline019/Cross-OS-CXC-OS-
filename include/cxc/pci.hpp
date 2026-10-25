#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::pci {

constexpr size_t maximum_devices = 128;

struct Device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t msi_capability;
    uint8_t msix_capability;
    uint16_t msi_control;
    uint16_t msix_control;
    uint32_t msix_table_bir;
    uint32_t msix_table_offset;
    uint32_t msix_pba_bir;
    uint32_t msix_pba_offset;
    uint32_t msix_table_entries;
};

uint16_t config_read16(const Device& device, uint8_t offset);
uint32_t config_read32(const Device& device, uint8_t offset);
void config_write16(const Device& device, uint8_t offset, uint16_t value);
void config_write32(const Device& device, uint8_t offset, uint32_t value);
uint32_t read_bar(const Device& device, uint8_t index);
uint8_t find_capability(const Device& device, uint8_t id);

// Enumerates PCI configuration mechanism 1 and safely walks MSI/MSI-X
// capability chains. It does not enable bus mastering or interrupt delivery.
bool enumerate();
size_t device_count();
const Device* devices();
size_t class_count(uint8_t class_code);
uint32_t msi_capable_count();
uint32_t msix_capable_count();
uint32_t capability_errors();

}  // namespace cxc::pci
