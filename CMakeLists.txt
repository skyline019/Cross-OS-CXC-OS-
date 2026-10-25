cmake_minimum_required(VERSION 3.20)

project(cxc_os VERSION 0.34.0 LANGUAGES CXX ASM)

if(NOT MINGW OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR
        "CXC/OS requires cmake/x86_64-uefi-mingw.cmake and an x86-64 MinGW-w64 GCC toolchain")
endif()

option(CXC_WARNINGS_AS_ERRORS "Treat kernel build warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(CMAKE_HOST_WIN32)
    find_program(CXC_OBJCOPY_EXECUTABLE NAMES objcopy REQUIRED)
else()
    find_program(CXC_OBJCOPY_EXECUTABLE
        NAMES x86_64-w64-mingw32-objcopy objcopy REQUIRED)
endif()
set(CXC_SMP_TRAMPOLINE_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/smp_trampoline_flat.o")
set(CXC_SMP_TRAMPOLINE_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/smp_trampoline.bin")
set(CXC_SMP_TRAMPOLINE_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/smp_trampoline_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_SMP_TRAMPOLINE_SOURCE}"
    COMMAND "${CMAKE_ASM_COMPILER}" -c
        "${CMAKE_CURRENT_SOURCE_DIR}/src/smp_trampoline.S"
        -o "${CXC_SMP_TRAMPOLINE_OBJECT}"
    COMMAND "${CXC_OBJCOPY_EXECUTABLE}" -O binary -j .smp_trampoline
        "${CXC_SMP_TRAMPOLINE_OBJECT}" "${CXC_SMP_TRAMPOLINE_BINARY}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_SMP_TRAMPOLINE_BINARY}"
        --output "${CXC_SMP_TRAMPOLINE_SOURCE}"
        --array trampoline_blob --size trampoline_blob_size
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/smp_trampoline.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
    BYPRODUCTS
        "${CXC_SMP_TRAMPOLINE_OBJECT}"
        "${CXC_SMP_TRAMPOLINE_BINARY}"
    COMMENT "Building position-independent SMP startup blob"
    VERBATIM
)

add_executable(cxc_kernel
    src/acpi.cpp
    src/apic.cpp
    src/e1000e.cpp
    src/e1000e_selftest.cpp
    src/edu.cpp
    src/gdt.S
    src/gdt.cpp
    src/heap.cpp
    src/idt.cpp
    src/interrupts.S
    src/ioapic.cpp
    src/irq.cpp
    src/kernel.cpp
    src/log.cpp
    src/memory.cpp
    src/msix.cpp
    src/net/checksum.cpp
    src/net/datagram.cpp
    src/net/proto.cpp
    src/net/socket.cpp
    src/pci.cpp
    src/pmm.cpp
    src/runtime_test.cpp
    src/runtime_probe.S
    src/scheduler.cpp
    src/self_test.cpp
    src/serial.cpp
    src/smp.cpp
    "${CXC_SMP_TRAMPOLINE_SOURCE}"
    src/stack.S
    src/uefi.cpp
    src/vmm.cpp
)
target_include_directories(cxc_kernel PRIVATE include src)
set_target_properties(cxc_kernel PROPERTIES
    PREFIX ""
    OUTPUT_NAME "BOOTX64"
    SUFFIX ".efi"
)

target_compile_options(cxc_kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        -ffreestanding
        -fno-builtin
        -fno-stack-protector
        -fno-pie
        -fno-use-cxa-atexit
        -fno-omit-frame-pointer
        -ffunction-sections
        -fdata-sections
        -mno-red-zone
        -mcmodel=large
        -Wall
        -Wextra
        -Wpedantic
    >
)
if(CXC_WARNINGS_AS_ERRORS)
    target_compile_options(cxc_kernel PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Werror>
    )
endif()

target_link_options(cxc_kernel PRIVATE
    -nostdlib
    -shared
    -Wl,--subsystem,10
    -Wl,--image-base,0xFFFFFFFF80000000
    -Wl,--entry,efi_main
    -Wl,--gc-sections
    -Wl,--no-insert-timestamp
    -Wl,--dynamicbase
    -Wl,--nxcompat
    -Wl,--exclude-all-symbols
)

set(CXC_ESP_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/cxc-os.img")
add_custom_command(
    OUTPUT "${CXC_ESP_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_esp.py"
        --kernel "$<TARGET_FILE:cxc_kernel>"
        --output "${CXC_ESP_IMAGE}"
    DEPENDS cxc_kernel "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_esp.py"
    COMMENT "Creating FAT32 UEFI system partition"
    VERBATIM
)
add_custom_target(esp_image ALL DEPENDS "${CXC_ESP_IMAGE}")

find_program(CXC_QEMU_EXECUTABLE NAMES qemu-system-x86_64)
if(CXC_QEMU_EXECUTABLE)
    add_custom_target(qemu
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --image "${CXC_ESP_IMAGE}"
            --qemu "${CXC_QEMU_EXECUTABLE}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/qemu"
        DEPENDS esp_image
        USES_TERMINAL
        VERBATIM
    )
    add_custom_target(qemu-test
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --image "${CXC_ESP_IMAGE}"
            --qemu "${CXC_QEMU_EXECUTABLE}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/qemu-test"
            --check
            --timeout 70
        DEPENDS esp_image
        USES_TERMINAL
        VERBATIM
    )
endif()

add_custom_target(release-validate-host
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/release_validate.py"
        --profile host
        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/release-validate-host"
    USES_TERMINAL
    VERBATIM
)

if(CXC_QEMU_EXECUTABLE)
    add_custom_target(release-validate
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/release_validate.py"
            --profile ci
            --image "${CXC_ESP_IMAGE}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/release-validate"
            --keep-work-dir
        DEPENDS esp_image
        USES_TERMINAL
        VERBATIM
    )
endif()

