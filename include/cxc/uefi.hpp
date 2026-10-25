#pragma once

#include <stdint.h>

// Minimal UEFI 2.x definitions required by the boot handoff. No EDK II,
// gnu-efi, Windows SDK, or hosted runtime headers are used.

#define EFIAPI __attribute__((ms_abi))

using EFI_HANDLE = void*;
using EFI_EVENT = void*;
using EFI_STATUS = uint64_t;
using EFI_PHYSICAL_ADDRESS = uint64_t;
using EFI_VIRTUAL_ADDRESS = uint64_t;
using CHAR16 = char16_t;
using BOOLEAN = uint8_t;

constexpr EFI_STATUS EFI_STATUS_ERROR_BIT = uint64_t{1} << 63;
constexpr EFI_STATUS EFI_SUCCESS = 0;
constexpr EFI_STATUS EFI_LOAD_ERROR = EFI_STATUS_ERROR_BIT | 1;
constexpr EFI_STATUS EFI_INVALID_PARAMETER = EFI_STATUS_ERROR_BIT | 2;
constexpr EFI_STATUS EFI_UNSUPPORTED = EFI_STATUS_ERROR_BIT | 3;
constexpr EFI_STATUS EFI_BUFFER_TOO_SMALL = EFI_STATUS_ERROR_BIT | 5;
constexpr EFI_STATUS EFI_DEVICE_ERROR = EFI_STATUS_ERROR_BIT | 7;

constexpr bool efi_error(EFI_STATUS status) {
    return (status & EFI_STATUS_ERROR_BIT) != 0;
}

struct EFI_TABLE_HEADER {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct EFI_GUID {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct EFI_SIMPLE_TEXT_OUTPUT_MODE;

using EFI_TEXT_RESET = EFI_STATUS(EFIAPI*)(void*, BOOLEAN);
using EFI_TEXT_STRING = EFI_STATUS(EFIAPI*)(void*, CHAR16*);
using EFI_TEXT_TEST_STRING = EFI_STATUS(EFIAPI*)(void*, CHAR16*);
using EFI_TEXT_QUERY_MODE = EFI_STATUS(EFIAPI*)(void*, uint64_t, uint64_t*, uint64_t*);
using EFI_TEXT_SET_MODE = EFI_STATUS(EFIAPI*)(void*, uint64_t);
using EFI_TEXT_SET_ATTRIBUTE = EFI_STATUS(EFIAPI*)(void*, uint64_t);
using EFI_TEXT_CLEAR_SCREEN = EFI_STATUS(EFIAPI*)(void*);
using EFI_TEXT_SET_CURSOR_POSITION = EFI_STATUS(EFIAPI*)(void*, uint64_t, uint64_t);
using EFI_TEXT_ENABLE_CURSOR = EFI_STATUS(EFIAPI*)(void*, BOOLEAN);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET reset;
    EFI_TEXT_STRING output_string;
    EFI_TEXT_TEST_STRING test_string;
    EFI_TEXT_QUERY_MODE query_mode;
    EFI_TEXT_SET_MODE set_mode;
    EFI_TEXT_SET_ATTRIBUTE set_attribute;
    EFI_TEXT_CLEAR_SCREEN clear_screen;
    EFI_TEXT_SET_CURSOR_POSITION set_cursor_position;
    EFI_TEXT_ENABLE_CURSOR enable_cursor;
    EFI_SIMPLE_TEXT_OUTPUT_MODE* mode;
};

enum EFI_MEMORY_TYPE : uint32_t {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14,
    EfiUnacceptedMemoryType = 15,
    EfiMaxMemoryType = 16,
};

enum EFI_ALLOCATE_TYPE : uint32_t {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
    MaxAllocateType = 3,
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t type;
    uint32_t padding;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40);

using EFI_ALLOCATE_PAGES = EFI_STATUS(EFIAPI*)(
    EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, uint64_t, EFI_PHYSICAL_ADDRESS*);
using EFI_FREE_PAGES = EFI_STATUS(EFIAPI*)(EFI_PHYSICAL_ADDRESS, uint64_t);
using EFI_GET_MEMORY_MAP = EFI_STATUS(EFIAPI*)(
    uint64_t*, EFI_MEMORY_DESCRIPTOR*, uint64_t*, uint64_t*, uint32_t*);
using EFI_ALLOCATE_POOL = EFI_STATUS(EFIAPI*)(EFI_MEMORY_TYPE, uint64_t, void**);
using EFI_FREE_POOL = EFI_STATUS(EFIAPI*)(void*);
using EFI_HANDLE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_HANDLE, EFI_GUID*, void**);
using EFI_EXIT_BOOT_SERVICES = EFI_STATUS(EFIAPI*)(EFI_HANDLE, uint64_t);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER header;
    void* raise_tpl;
    void* restore_tpl;
    EFI_ALLOCATE_PAGES allocate_pages;
    EFI_FREE_PAGES free_pages;
    EFI_GET_MEMORY_MAP get_memory_map;
    EFI_ALLOCATE_POOL allocate_pool;
    EFI_FREE_POOL free_pool;
    void* create_event;
    void* set_timer;
    void* wait_for_event;
    void* signal_event;
    void* close_event;
    void* check_event;
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    EFI_HANDLE_PROTOCOL handle_protocol;
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    void* load_image;
    void* start_image;
    void* exit;
    void* unload_image;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    void* get_next_monotonic_count;
    void* stall;
    void* set_watchdog_timer;
    void* connect_controller;
    void* disconnect_controller;
    void* open_protocol;
    void* close_protocol;
    void* open_protocol_information;
    void* protocols_per_handle;
    void* locate_handle_buffer;
    void* locate_protocol;
    void* install_multiple_protocol_interfaces;
    void* uninstall_multiple_protocol_interfaces;
    void* calculate_crc32;
    void* copy_mem;
    void* set_mem;
    void* create_event_ex;
};

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_RUNTIME_SERVICES;

struct EFI_CONFIGURATION_TABLE {
    EFI_GUID vendor_guid;
    void* vendor_table;
};

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER header;
    CHAR16* firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* std_err;
    EFI_RUNTIME_SERVICES* runtime_services;
    EFI_BOOT_SERVICES* boot_services;
    uint64_t number_of_table_entries;
    EFI_CONFIGURATION_TABLE* configuration_table;
};

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE* system_table;
    EFI_HANDLE device_handle;
    void* file_path;
    void* reserved;
    uint32_t load_options_size;
    void* load_options;
    void* image_base;
    uint64_t image_size;
    EFI_MEMORY_TYPE image_code_type;
    EFI_MEMORY_TYPE image_data_type;
    void* unload;
};

struct EFI_FILE_PROTOCOL;

using EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME =
    EFI_STATUS(EFIAPI*)(void*, EFI_FILE_PROTOCOL**);
using EFI_FILE_OPEN = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**,
                                          CHAR16*, uint64_t, uint64_t);
using EFI_FILE_CLOSE = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*);
using EFI_FILE_DELETE = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*);
using EFI_FILE_READ = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, uint64_t*, void*);
using EFI_FILE_WRITE = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, uint64_t*, void*);
using EFI_FILE_GET_POSITION = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, uint64_t*);
using EFI_FILE_SET_POSITION = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, uint64_t);
using EFI_FILE_GET_INFO = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, EFI_GUID*,
                                              uint64_t*, void*);
using EFI_FILE_SET_INFO = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*, EFI_GUID*,
                                              uint64_t, void*);
using EFI_FILE_FLUSH = EFI_STATUS(EFIAPI*)(EFI_FILE_PROTOCOL*);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t revision;
    EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME open_volume;
};

struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    EFI_FILE_DELETE delete_file;
    EFI_FILE_READ read;
    EFI_FILE_WRITE write;
    EFI_FILE_GET_POSITION get_position;
    EFI_FILE_SET_POSITION set_position;
    EFI_FILE_GET_INFO get_info;
    EFI_FILE_SET_INFO set_info;
    EFI_FILE_FLUSH flush;
};

constexpr uint64_t EFI_FILE_MODE_READ = 0x0000000000000001ull;

namespace cxc::uefi {

struct BootMemoryMap {
    EFI_MEMORY_DESCRIPTOR* descriptors = nullptr;
    uint64_t size = 0;
    uint64_t descriptor_size = 0;
    uint32_t descriptor_version = 0;
};

EFI_STATUS loaded_image_info(EFI_HANDLE image_handle,
                             EFI_SYSTEM_TABLE* system_table,
                             uint64_t& image_base,
                             uint64_t& image_size);

// Returns a firmware physical RSDP pointer, preferring ACPI 2.0+ over 1.0.
// The pointer must be consumed before runtime page tables remove low mappings.
EFI_STATUS find_acpi_rsdp(EFI_SYSTEM_TABLE* system_table, uint64_t& rsdp);

// Allocates contiguous loader-owned pages at or below max_address. The call
// happens before the final memory-map snapshot used by ExitBootServices.
EFI_STATUS allocate_low_pages(EFI_SYSTEM_TABLE* system_table, uint64_t pages,
                              uint64_t max_address, uint64_t& physical);

// On success, Boot Services are no longer callable and `map` remains allocated
// as EfiLoaderData. On failure, Boot Services are still active.
EFI_STATUS exit_boot_services(EFI_HANDLE image_handle,
                              EFI_SYSTEM_TABLE* system_table,
                              BootMemoryMap& map);

// Read a path from the LoadedImage device ESP into EfiLoaderData pages that
// survive ExitBootServices. `physical`/`size` are filled on success.
EFI_STATUS load_esp_file(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table,
                         const CHAR16* path, uint64_t max_bytes,
                         uint64_t& physical, uint64_t& size);

}  // namespace cxc::uefi
