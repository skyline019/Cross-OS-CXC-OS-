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

EFI_STATUS load_esp_file(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table,
                         const CHAR16* path, uint64_t max_bytes,
                         uint64_t& physical, uint64_t& size) {
    physical = 0;
    size = 0;
    if (system_table == nullptr || system_table->boot_services == nullptr ||
        system_table->boot_services->handle_protocol == nullptr ||
        system_table->boot_services->allocate_pages == nullptr ||
        path == nullptr || max_bytes == 0) {
        return EFI_INVALID_PARAMETER;
    }
    EFI_BOOT_SERVICES* boot = system_table->boot_services;

    EFI_GUID loaded_image_guid = {
        0x5b1b31a1, 0x9562, 0x11d2,
        {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
    };
    EFI_LOADED_IMAGE_PROTOCOL* loaded = nullptr;
    EFI_STATUS status = boot->handle_protocol(
        image_handle, &loaded_image_guid, reinterpret_cast<void**>(&loaded));
    if (efi_error(status) || loaded == nullptr ||
        loaded->device_handle == nullptr) {
        return efi_error(status) ? status : EFI_UNSUPPORTED;
    }

    EFI_GUID file_system_guid = {
        0x964e5b22, 0x6459, 0x11d2,
        {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
    };
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* volume_proto = nullptr;
    status = boot->handle_protocol(loaded->device_handle, &file_system_guid,
                                   reinterpret_cast<void**>(&volume_proto));
    if (efi_error(status) || volume_proto == nullptr ||
        volume_proto->open_volume == nullptr) {
        return efi_error(status) ? status : EFI_UNSUPPORTED;
    }

    EFI_FILE_PROTOCOL* root = nullptr;
    status = volume_proto->open_volume(volume_proto, &root);
    if (efi_error(status) || root == nullptr || root->open == nullptr ||
        root->close == nullptr) {
        return efi_error(status) ? status : EFI_UNSUPPORTED;
    }

    EFI_FILE_PROTOCOL* file = nullptr;
    status = root->open(root, &file, const_cast<CHAR16*>(path),
                        EFI_FILE_MODE_READ, 0);
    if (efi_error(status) || file == nullptr) {
        root->close(root);
        return efi_error(status) ? status : EFI_LOAD_ERROR;
    }
    if (file->set_position == nullptr || file->get_position == nullptr ||
        file->read == nullptr || file->close == nullptr) {
        file->close(file);
        root->close(root);
        return EFI_UNSUPPORTED;
    }

    status = file->set_position(file, 0xffffffffffffffffull);
    if (efi_error(status)) {
        file->close(file);
        root->close(root);
        return status;
    }
    uint64_t file_size = 0;
    status = file->get_position(file, &file_size);
    if (efi_error(status) || file_size == 0 || file_size > max_bytes) {
        file->close(file);
        root->close(root);
        return efi_error(status) ? status : EFI_LOAD_ERROR;
    }
    status = file->set_position(file, 0);
    if (efi_error(status)) {
        file->close(file);
        root->close(root);
        return status;
    }

    const uint64_t pages = (file_size + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS address = 0;
    status = boot->allocate_pages(AllocateAnyPages, EfiLoaderData, pages,
                                  &address);
    if (efi_error(status)) {
        file->close(file);
        root->close(root);
        return status;
    }

    uint64_t remaining = file_size;
    uint8_t* cursor = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
    while (remaining != 0) {
        uint64_t chunk = remaining;
        status = file->read(file, &chunk, cursor);
        if (efi_error(status) || chunk == 0 || chunk > remaining) {
            // Leave the pages allocated so a failure still leaves a clear
            // signature in the memory map; caller will not ExitBootServices.
            file->close(file);
            root->close(root);
            return efi_error(status) ? status : EFI_LOAD_ERROR;
        }
        cursor += chunk;
        remaining -= chunk;
    }

    file->close(file);
    root->close(root);
    physical = address;
    size = file_size;
    return EFI_SUCCESS;
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
