#include <cxc/uefi.hpp>

#include <cxc/log.hpp>

namespace {

bool same_guid(const EFI_GUID& left, const EFI_GUID& right) {
    if (left.data1 != right.data1 || left.data2 != right.data2 ||
        left.data3 != right.data3) {
        return false;
    }
    for (size_t index = 0; index < 8; ++index) {
        if (left.data4[index] != right.data4[index]) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace cxc::uefi {

EFI_STATUS loaded_image_info(EFI_HANDLE image_handle,
                             EFI_SYSTEM_TABLE* system_table,
                             uint64_t& image_base,
                             uint64_t& image_size) {
    if (system_table == nullptr || system_table->boot_services == nullptr ||
        system_table->boot_services->handle_protocol == nullptr) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_GUID loaded_image_guid = {
        0x5b1b31a1, 0x9562, 0x11d2,
        {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
    };
    EFI_LOADED_IMAGE_PROTOCOL* loaded = nullptr;
    const EFI_STATUS status = system_table->boot_services->handle_protocol(
        image_handle, &loaded_image_guid,
        reinterpret_cast<void**>(&loaded));
    if (efi_error(status) || loaded == nullptr || loaded->image_base == nullptr) {
        return efi_error(status) ? status : EFI_UNSUPPORTED;
    }

    image_base = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(loaded->image_base));
    image_size = loaded->image_size;
    return image_size == 0 ? EFI_LOAD_ERROR : EFI_SUCCESS;
}

EFI_STATUS find_acpi_rsdp(EFI_SYSTEM_TABLE* system_table, uint64_t& rsdp) {
    if (system_table == nullptr || system_table->configuration_table == nullptr) {
        return EFI_INVALID_PARAMETER;
    }
    const EFI_GUID acpi20 = {
        0x8868e871, 0xe4f1, 0x11d3,
        {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81},
    };
    const EFI_GUID acpi10 = {
        0xeb9d2d30, 0x2d88, 0x11d3,
        {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d},
    };

    uint64_t fallback = 0;
    for (uint64_t index = 0; index < system_table->number_of_table_entries;
         ++index) {
        const EFI_CONFIGURATION_TABLE& entry =
            system_table->configuration_table[index];
        if (entry.vendor_table == nullptr) {
            continue;
        }
        const uint64_t address = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(entry.vendor_table));
        if (same_guid(entry.vendor_guid, acpi20)) {
            rsdp = address;
            return EFI_SUCCESS;
        }
        if (same_guid(entry.vendor_guid, acpi10)) {
            fallback = address;
        }
    }
    if (fallback != 0) {
        rsdp = fallback;
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

EFI_STATUS allocate_low_pages(EFI_SYSTEM_TABLE* system_table, uint64_t pages,
                              uint64_t max_address, uint64_t& physical) {
    physical = 0;
    if (system_table == nullptr || system_table->boot_services == nullptr ||
        system_table->boot_services->allocate_pages == nullptr || pages == 0 ||
        max_address == ~uint64_t{0} || pages > (max_address + 1) / 4096) {
        return EFI_INVALID_PARAMETER;
    }
    const uint64_t limit = max_address + 1;
    const uint64_t bytes = pages * 4096;
    EFI_PHYSICAL_ADDRESS address = max_address;
    const EFI_STATUS status = system_table->boot_services->allocate_pages(
        AllocateMaxAddress, EfiLoaderData, pages, &address);
    if (efi_error(status)) {
        return status;
    }
    if ((address & 4095) != 0 || address > max_address ||
        bytes > limit - address) {
        // Do not call FreePages here: this validation is only reached if the
        // firmware violated AllocateMaxAddress, and preserving its state is
        // less useful than returning a visible error to the caller.
        return EFI_LOAD_ERROR;
    }
    physical = address;
    return EFI_SUCCESS;
}

EFI_STATUS exit_boot_services(EFI_HANDLE image_handle,
                              EFI_SYSTEM_TABLE* system_table,
                              BootMemoryMap& map) {
    if (system_table == nullptr || system_table->boot_services == nullptr) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_BOOT_SERVICES* boot = system_table->boot_services;
    if (boot->get_memory_map == nullptr || boot->allocate_pool == nullptr ||
        boot->free_pool == nullptr || boot->exit_boot_services == nullptr) {
        return EFI_UNSUPPORTED;
    }

    for (uint32_t attempt = 0; attempt < 6; ++attempt) {
        uint64_t map_size = 0;
        uint64_t map_key = 0;
        uint64_t descriptor_size = 0;
        uint32_t descriptor_version = 0;

        EFI_STATUS status = boot->get_memory_map(
            &map_size, nullptr, &map_key, &descriptor_size, &descriptor_version);
        if (status != EFI_BUFFER_TOO_SMALL || descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
            return status == EFI_BUFFER_TOO_SMALL ? EFI_DEVICE_ERROR : status;
        }

        // Leave room for the AllocatePool call itself and a few descriptor
        // changes before the final GetMemoryMap snapshot.
        map_size += descriptor_size * 8 + 4096;
        void* buffer = nullptr;
        status = boot->allocate_pool(EfiLoaderData, map_size, &buffer);
        if (efi_error(status)) {
            return status;
        }

        uint64_t actual_size = map_size;
        status = boot->get_memory_map(&actual_size,
                                      static_cast<EFI_MEMORY_DESCRIPTOR*>(buffer),
                                      &map_key, &descriptor_size,
                                      &descriptor_version);
        if (efi_error(status)) {
            boot->free_pool(buffer);
            if (status == EFI_BUFFER_TOO_SMALL) {
                continue;
            }
            return status;
        }

        // Do not log or call any other Boot Service between this snapshot and
        // ExitBootServices: doing so could invalidate map_key.
        status = boot->exit_boot_services(image_handle, map_key);
        if (!efi_error(status)) {
            map.descriptors = static_cast<EFI_MEMORY_DESCRIPTOR*>(buffer);
            map.size = actual_size;
            map.descriptor_size = descriptor_size;
            map.descriptor_version = descriptor_version;
            log::disable_uefi_console();
            return EFI_SUCCESS;
        }

        boot->free_pool(buffer);
        log::line("[boot] ExitBootServices key changed; retrying");
    }

    return EFI_DEVICE_ERROR;
}

}  // namespace cxc::uefi
