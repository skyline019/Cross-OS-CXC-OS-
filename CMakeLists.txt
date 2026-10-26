cmake_minimum_required(VERSION 3.20)

project(cxc_os VERSION 2.1.0 LANGUAGES CXX ASM)

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

set(CXC_USER_HELLO_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_hello.elf")
set(CXC_USER_HELLO_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/user_hello_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_HELLO_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_hello_elf.py"
        --output "${CXC_USER_HELLO_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_HELLO_ELF}"
        --output "${CXC_USER_HELLO_SOURCE}"
        --array hello_elf --size hello_elf_size
        --namespace "cxc::user"
        --header "cxc/user_hello_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_hello_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_hello_blob.hpp"
    BYPRODUCTS "${CXC_USER_HELLO_ELF}"
    COMMENT "Building embedded static user hello ELF"
    VERBATIM
)

set(CXC_USER_FS_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_fs.elf")
set(CXC_USER_FS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/user_fs_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_FS_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fs_elf.py"
        --output "${CXC_USER_FS_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_FS_ELF}"
        --output "${CXC_USER_FS_SOURCE}"
        --array fs_elf --size fs_elf_size
        --namespace "cxc::user"
        --header "cxc/user_fs_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fs_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_fs_blob.hpp"
    BYPRODUCTS "${CXC_USER_FS_ELF}"
    COMMENT "Building embedded static user FS ELF"
    VERBATIM
)

set(CXC_USER_BRK_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_brk.elf")
set(CXC_USER_BRK_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/user_brk_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_BRK_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_brk_elf.py"
        --output "${CXC_USER_BRK_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_BRK_ELF}"
        --output "${CXC_USER_BRK_SOURCE}"
        --array brk_elf --size brk_elf_size
        --namespace "cxc::user"
        --header "cxc/user_brk_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_brk_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_brk_blob.hpp"
    BYPRODUCTS "${CXC_USER_BRK_ELF}"
    COMMENT "Building embedded static user brk ELF"
    VERBATIM
)

set(CXC_USER_MMAP_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_mmap.elf")
set(CXC_USER_MMAP_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/user_mmap_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_MMAP_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_mmap_elf.py"
        --output "${CXC_USER_MMAP_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_MMAP_ELF}"
        --output "${CXC_USER_MMAP_SOURCE}"
        --array mmap_elf --size mmap_elf_size
        --namespace "cxc::user"
        --header "cxc/user_mmap_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_mmap_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_mmap_blob.hpp"
    BYPRODUCTS "${CXC_USER_MMAP_ELF}"
    COMMENT "Building embedded static user mmap ELF"
    VERBATIM
)

set(CXC_USER_DIR_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_dir.elf")
set(CXC_USER_DIR_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/user_dir_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_DIR_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dir_elf.py"
        --output "${CXC_USER_DIR_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_DIR_ELF}"
        --output "${CXC_USER_DIR_SOURCE}"
        --array dir_elf --size dir_elf_size
        --namespace "cxc::user"
        --header "cxc/user_dir_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_dir_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_dir_blob.hpp"
    BYPRODUCTS "${CXC_USER_DIR_ELF}"
    COMMENT "Building embedded static user dir ELF"
    VERBATIM
)

set(CXC_USER_MUNMAP_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_munmap.elf")
set(CXC_USER_MUNMAP_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/user_munmap_blob.cpp")
add_custom_command(
    OUTPUT "${CXC_USER_MUNMAP_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_munmap_elf.py"
        --output "${CXC_USER_MUNMAP_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        --input "${CXC_USER_MUNMAP_ELF}"
        --output "${CXC_USER_MUNMAP_SOURCE}"
        --array munmap_elf --size munmap_elf_size
        --namespace "cxc::user"
        --header "cxc/user_munmap_blob.hpp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_munmap_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/user_munmap_blob.hpp"
    BYPRODUCTS "${CXC_USER_MUNMAP_ELF}"
    COMMENT "Building embedded static user munmap ELF"
    VERBATIM
)

add_executable(cxc_kernel
    src/acpi.cpp
    src/apic.cpp
    src/e1000e.cpp
    src/e1000e_selftest.cpp
    src/edu.cpp
    src/elf.cpp
    src/fs.cpp
    src/gdt.S
    src/gdt.cpp
    src/heap.cpp
    src/idt.cpp
    src/interrupts.S
    src/syscall_entry.S
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
    src/syscall.cpp
    src/user.cpp
    "${CXC_SMP_TRAMPOLINE_SOURCE}"
    "${CXC_USER_HELLO_SOURCE}"
    "${CXC_USER_FS_SOURCE}"
    "${CXC_USER_BRK_SOURCE}"
    "${CXC_USER_MMAP_SOURCE}"
    "${CXC_USER_DIR_SOURCE}"
    "${CXC_USER_MUNMAP_SOURCE}"
    src/stack.S
    src/uefi.cpp
    src/virtio_blk.cpp
    src/cxcfs.cpp
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

set(CXC_USER_EXT_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_ext.elf")
add_custom_command(
    OUTPUT "${CXC_USER_EXT_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_ext_elf.py"
        --output "${CXC_USER_EXT_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_ext_elf.py"
    COMMENT "Building ESP-resident static user EXT ELF"
    VERBATIM
)

set(CXC_USER_CROSS_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_cross.elf")
find_program(CXC_USER_ELF_CC NAMES gcc)
find_program(CXC_MUSL_GCC NAMES musl-gcc)
set(CXC_USER_CROSS_USES_GCC FALSE)
if(CXC_USER_ELF_CC)
    execute_process(
        COMMAND "${CXC_USER_ELF_CC}" -dumpmachine
        OUTPUT_VARIABLE CXC_USER_ELF_CC_MACHINE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(CXC_USER_ELF_CC_MACHINE MATCHES "linux")
        set(CXC_USER_CROSS_USES_GCC TRUE)
    endif()
endif()
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_CROSS_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_CROSS_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP CROSS.ELF via host gcc (ELF64 ET_EXEC)"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_CROSS_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cross_elf.py"
            --output "${CXC_USER_CROSS_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cross_elf.py"
        COMMENT "Building ESP CROSS.ELF via handmade fallback (no ELF gcc)"
        VERBATIM
    )
endif()

set(CXC_USER_LSEEK_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_lseek.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LSEEK_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LSEEK_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/lseek_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/lseek_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LSEEK.ELF via host gcc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LSEEK_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lseek_elf.py"
            --output "${CXC_USER_LSEEK_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lseek_elf.py"
        COMMENT "Building ESP LSEEK.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_FSTAT_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_fstat.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_FSTAT_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_FSTAT_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/fstat_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/fstat_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP FSTAT.ELF via host gcc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_FSTAT_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fstat_elf.py"
            --output "${CXC_USER_FSTAT_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fstat_elf.py"
        COMMENT "Building ESP FSTAT.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_STAT_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_stat.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_STAT_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_STAT_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/stat_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/stat_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP STAT.ELF via host gcc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_STAT_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_stat_elf.py"
            --output "${CXC_USER_STAT_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_stat_elf.py"
        COMMENT "Building ESP STAT.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_GETDENTS_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_getdents.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_GETDENTS_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_GETDENTS_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/getdents_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/getdents_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP DENTS.ELF via host gcc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_GETDENTS_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_getdents_elf.py"
            --output "${CXC_USER_GETDENTS_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_getdents_elf.py"
        COMMENT "Building ESP DENTS.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_ARCH_PRCTL_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_arch_prctl.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_ARCH_PRCTL_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_ARCH_PRCTL_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/arch_prctl_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/arch_prctl_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP PRCTL.ELF via host gcc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_ARCH_PRCTL_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_arch_prctl_elf.py"
            --output "${CXC_USER_ARCH_PRCTL_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_arch_prctl_elf.py"
        COMMENT "Building ESP PRCTL.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBC.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_elf.py"
            --output "${CXC_USER_LIBC_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_elf.py"
        COMMENT "Building ESP LIBC.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_FS_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_fs.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_FS_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_FS_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_fs_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_fs_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/fcntl.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBFS.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_FS_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_fs_elf.py"
            --output "${CXC_USER_LIBC_FS_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_fs_elf.py"
        COMMENT "Building ESP LIBFS.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_SBRK_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_sbrk.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_SBRK_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_SBRK_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_sbrk_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_sbrk_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBBRK.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_SBRK_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_sbrk_elf.py"
            --output "${CXC_USER_LIBC_SBRK_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_sbrk_elf.py"
        COMMENT "Building ESP LIBBRK.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_MALLOC_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_malloc.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_MALLOC_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_MALLOC_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_malloc_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/malloc.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_malloc_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/malloc.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/stdlib.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBMAL.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_MALLOC_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_malloc_elf.py"
            --output "${CXC_USER_LIBC_MALLOC_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_malloc_elf.py"
        COMMENT "Building ESP LIBMAL.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_ERRNO_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_errno.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_ERRNO_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_ERRNO_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_errno_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_errno_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/fcntl.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBERR.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_ERRNO_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_errno_elf.py"
            --output "${CXC_USER_LIBC_ERRNO_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_errno_elf.py"
        COMMENT "Building ESP LIBERR.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_STRING_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_string.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_STRING_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_STRING_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_string_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_string_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBSTR.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_STRING_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_string_elf.py"
            --output "${CXC_USER_LIBC_STRING_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_string_elf.py"
        COMMENT "Building ESP LIBSTR.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_WRITEV_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_writev.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_WRITEV_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_WRITEV_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_writev_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_writev_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBWRV.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_WRITEV_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_writev_elf.py"
            --output "${CXC_USER_LIBC_WRITEV_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_writev_elf.py"
        COMMENT "Building ESP LIBWRV.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_CRT_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_crt.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_CRT_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_CRT_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_crt_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_crt_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBCRT.ELF via host gcc + mini_libc crt0"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_CRT_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_crt_elf.py"
            --output "${CXC_USER_LIBC_CRT_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_crt_elf.py"
        COMMENT "Building ESP LIBCRT.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_UNAME_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_uname.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_UNAME_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_UNAME_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_uname_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_uname_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/utsname.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBUNA.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_UNAME_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_uname_elf.py"
            --output "${CXC_USER_LIBC_UNAME_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_uname_elf.py"
        COMMENT "Building ESP LIBUNA.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_TID_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_tid.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_TID_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_TID_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_tid_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_tid_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBTID.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_TID_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_tid_elf.py"
            --output "${CXC_USER_LIBC_TID_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_tid_elf.py"
        COMMENT "Building ESP LIBTID.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_LIBC_PID_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_libc_pid.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_PID_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_LIBC_PID_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_pid_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_pid_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBPID.ELF via host gcc + mini_libc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_LIBC_PID_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_pid_elf.py"
            --output "${CXC_USER_LIBC_PID_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_pid_elf.py"
        COMMENT "Building ESP LIBPID.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_WRITE_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_write.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_WRITE_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_MUSL_WRITE_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_write_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/syscall.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_write_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBMSL.ELF via host gcc + musl_subset"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_WRITE_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_write_elf.py"
            --output "${CXC_USER_MUSL_WRITE_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_write_elf.py"
        COMMENT "Building ESP LIBMSL.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_AUXV_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_auxv.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_AUXV_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
            -o "${CXC_USER_MUSL_AUXV_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_auxv_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/syscall.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_auxv_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/unistd.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        COMMENT "Building ESP LIBMAV.ELF via host gcc + musl_subset"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_AUXV_ELF}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_auxv_elf.py"
            --output "${CXC_USER_MUSL_AUXV_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_auxv_elf.py"
        COMMENT "Building ESP LIBMAV.ELF via handmade fallback"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_CRT_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_crt.elf")
set(CXC_USER_MUSL_CRT_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_crt.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_CRT_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_CRT_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBMCR.ELF via musl-gcc static CRT"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_CRT_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_CRT_PREBUILT}"
            "${CXC_USER_MUSL_CRT_ELF}"
        DEPENDS "${CXC_USER_MUSL_CRT_PREBUILT}"
        COMMENT "Copying prebuilt musl CRT ESP LIBMCR.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_MALLOC_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_malloc.elf")
set(CXC_USER_MUSL_MALLOC_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_malloc.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_MALLOC_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_MALLOC_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_malloc_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_malloc_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBMMA.ELF via musl-gcc malloc"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_MALLOC_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_MALLOC_PREBUILT}"
            "${CXC_USER_MUSL_MALLOC_ELF}"
        DEPENDS "${CXC_USER_MUSL_MALLOC_PREBUILT}"
        COMMENT "Copying prebuilt musl malloc ESP LIBMMA.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_PRINTF_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_printf.elf")
set(CXC_USER_MUSL_PRINTF_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_printf.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_PRINTF_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_PRINTF_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_printf_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_printf_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBMPR.ELF via musl-gcc printf"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_PRINTF_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_PRINTF_PREBUILT}"
            "${CXC_USER_MUSL_PRINTF_ELF}"
        DEPENDS "${CXC_USER_MUSL_PRINTF_PREBUILT}"
        COMMENT "Copying prebuilt musl printf ESP LIBMPR.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_FOPEN_ELF "${CMAKE_CURRENT_BINARY_DIR}/user_musl_fopen.elf")
set(CXC_USER_MUSL_FOPEN_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_fopen.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_FOPEN_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_FOPEN_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_fopen_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_fopen_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBFOP.ELF via musl-gcc fopen"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_FOPEN_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_FOPEN_PREBUILT}"
            "${CXC_USER_MUSL_FOPEN_ELF}"
        DEPENDS "${CXC_USER_MUSL_FOPEN_PREBUILT}"
        COMMENT "Copying prebuilt musl fopen ESP LIBFOP.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_OPENDIR_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_musl_opendir.elf")
set(CXC_USER_MUSL_OPENDIR_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_opendir.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_OPENDIR_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_OPENDIR_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_opendir_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_opendir_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBODR.ELF via musl-gcc opendir"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_OPENDIR_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_OPENDIR_PREBUILT}"
            "${CXC_USER_MUSL_OPENDIR_ELF}"
        DEPENDS "${CXC_USER_MUSL_OPENDIR_PREBUILT}"
        COMMENT "Copying prebuilt musl opendir ESP LIBODR.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_UNLINK_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_musl_unlink.elf")
set(CXC_USER_MUSL_UNLINK_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_unlink.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_UNLINK_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_UNLINK_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_unlink_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_unlink_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBUNL.ELF via musl-gcc unlink"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_UNLINK_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_UNLINK_PREBUILT}"
            "${CXC_USER_MUSL_UNLINK_ELF}"
        DEPENDS "${CXC_USER_MUSL_UNLINK_PREBUILT}"
        COMMENT "Copying prebuilt musl unlink ESP LIBUNL.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_BLKFS_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_musl_blkfs.elf")
set(CXC_USER_MUSL_BLKFS_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_blkfs.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_BLKFS_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_BLKFS_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkfs_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkfs_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBBLK.ELF via musl-gcc blkfs"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_BLKFS_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_BLKFS_PREBUILT}"
            "${CXC_USER_MUSL_BLKFS_ELF}"
        DEPENDS "${CXC_USER_MUSL_BLKFS_PREBUILT}"
        COMMENT "Copying prebuilt musl blkfs ESP LIBBLK.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_MUSL_BLKDIR_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_musl_blkdir.elf")
set(CXC_USER_MUSL_BLKDIR_PREBUILT
    "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_blkdir.elf")
if(CXC_MUSL_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_BLKDIR_ELF}"
        COMMAND "${CXC_MUSL_GCC}"
            -static -fno-pie -no-pie -fno-stack-protector
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
            -o "${CXC_USER_MUSL_BLKDIR_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkdir_probe.c"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkdir_probe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        COMMENT "Building ESP LIBDIR.ELF via musl-gcc blkdir"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_MUSL_BLKDIR_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_MUSL_BLKDIR_PREBUILT}"
            "${CXC_USER_MUSL_BLKDIR_ELF}"
        DEPENDS "${CXC_USER_MUSL_BLKDIR_PREBUILT}"
        COMMENT "Copying prebuilt musl blkdir ESP LIBDIR.ELF"
        VERBATIM
    )
endif()

set(CXC_USER_DISK_EXEC_STUB_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_execve_stub.elf")
set(CXC_USER_DISK_EXEC_PAYLOAD_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_disk_exec.elf")
add_custom_command(
    OUTPUT "${CXC_USER_DISK_EXEC_STUB_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_execve_stub_elf.py"
        --output "${CXC_USER_DISK_EXEC_STUB_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_execve_stub_elf.py"
    COMMENT "Building ESP EXSTB.ELF execve stub"
    VERBATIM
)
add_custom_command(
    OUTPUT "${CXC_USER_DISK_EXEC_PAYLOAD_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_disk_exec_elf.py"
        --output "${CXC_USER_DISK_EXEC_PAYLOAD_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_disk_exec_elf.py"
    COMMENT "Building ESP DEXEC.ELF disk-exec payload"
    VERBATIM
)

set(CXC_USER_FORK_WAIT_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_fork_wait.elf")
add_custom_command(
    OUTPUT "${CXC_USER_FORK_WAIT_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fork_wait_elf.py"
        --output "${CXC_USER_FORK_WAIT_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fork_wait_elf.py"
    COMMENT "Building ESP FORK.ELF fork/wait probe"
    VERBATIM
)

set(CXC_USER_PIPE_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_pipe.elf")
add_custom_command(
    OUTPUT "${CXC_USER_PIPE_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_pipe_elf.py"
        --output "${CXC_USER_PIPE_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_pipe_elf.py"
    COMMENT "Building ESP PIPE.ELF pipe/dup probe"
    VERBATIM
)

set(CXC_USER_CHDIR_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_chdir.elf")
add_custom_command(
    OUTPUT "${CXC_USER_CHDIR_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_chdir_elf.py"
        --output "${CXC_USER_CHDIR_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_chdir_elf.py"
    COMMENT "Building ESP CHDIR.ELF chdir/getcwd probe"
    VERBATIM
)

set(CXC_USER_NEST_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_nest.elf")
add_custom_command(
    OUTPUT "${CXC_USER_NEST_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_nest_elf.py"
        --output "${CXC_USER_NEST_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_nest_elf.py"
    COMMENT "Building ESP NEST.ELF nested-dir probe"
    VERBATIM
)

set(CXC_USER_CAP_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_cap.elf")
add_custom_command(
    OUTPUT "${CXC_USER_CAP_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cap_elf.py"
        --output "${CXC_USER_CAP_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cap_elf.py"
    COMMENT "Building ESP CAP.ELF capacity probe"
    VERBATIM
)

set(CXC_USER_LOAD128_STUB_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_load128_stub.elf")
add_custom_command(
    OUTPUT "${CXC_USER_LOAD128_STUB_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_stub_elf.py"
        --output "${CXC_USER_LOAD128_STUB_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_stub_elf.py"
    COMMENT "Building ESP L128S.ELF load128 execve stub"
    VERBATIM
)

set(CXC_USER_LOAD128_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_load128.elf")
add_custom_command(
    OUTPUT "${CXC_USER_LOAD128_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_elf.py"
        --output "${CXC_USER_LOAD128_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_elf.py"
    COMMENT "Building ESP L128.ELF load128 payload"
    VERBATIM
)

set(CXC_USER_HEAP_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_heap.elf")
add_custom_command(
    OUTPUT "${CXC_USER_HEAP_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_heap_elf.py"
        --output "${CXC_USER_HEAP_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_heap_elf.py"
    COMMENT "Building ESP HEAP.ELF brk/mmap expand probe"
    VERBATIM
)

set(CXC_USER_TIME_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_time.elf")
add_custom_command(
    OUTPUT "${CXC_USER_TIME_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_time_elf.py"
        --output "${CXC_USER_TIME_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_time_elf.py"
    COMMENT "Building ESP TIME.ELF access/time probe"
    VERBATIM
)

set(CXC_USER_ONBOX_STUB_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_onbox_stub.elf")
add_custom_command(
    OUTPUT "${CXC_USER_ONBOX_STUB_ELF}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_onbox_stub_elf.py"
        --output "${CXC_USER_ONBOX_STUB_ELF}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_onbox_stub_elf.py"
    COMMENT "Building ESP ONBXST.ELF on-box cc stub"
    VERBATIM
)

set(CXC_USER_ONBOX_CC_ELF
    "${CMAKE_CURRENT_BINARY_DIR}/user_onbox_cc.elf")
if(CXC_USER_CROSS_USES_GCC)
    add_custom_command(
        OUTPUT "${CXC_USER_ONBOX_CC_ELF}"
        COMMAND "${CXC_USER_ELF_CC}"
            -nostdlib -static -fno-pie -no-pie
            -fno-stack-protector -fno-asynchronous-unwind-tables
            -Os
            "-I${CMAKE_CURRENT_SOURCE_DIR}/user"
            -Wl,--build-id=none
            "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc.ld"
            -o "${CXC_USER_ONBOX_CC_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/main.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/lex.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/parse.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/emit.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/prep.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/elflink.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/malloc.c"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CXC_USER_ONBOX_CC_ELF}"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/cxc_cc.elf"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/main.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/lex.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/parse.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/emit.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/prep.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/elflink.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/cc.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/malloc.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc.ld"
        COMMENT "Building ESP ONBXCC.ELF freestanding mini-cc (A2)"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CXC_USER_ONBOX_CC_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/cxc_cc.elf"
            "${CXC_USER_ONBOX_CC_ELF}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/cxc_cc.elf"
        COMMENT "Copying prebuilt ONBXCC.ELF mini-cc"
        VERBATIM
    )
endif()

set(CXC_ESP_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/cxc-os.img")
add_custom_command(
    OUTPUT "${CXC_ESP_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_esp.py"
        --kernel "$<TARGET_FILE:cxc_kernel>"
        --extra "EXT     ELF" "${CXC_USER_EXT_ELF}"
        --extra "CROSS   ELF" "${CXC_USER_CROSS_ELF}"
        --extra "LSEEK   ELF" "${CXC_USER_LSEEK_ELF}"
        --extra "FSTAT   ELF" "${CXC_USER_FSTAT_ELF}"
        --extra "STAT    ELF" "${CXC_USER_STAT_ELF}"
        --extra "DENTS   ELF" "${CXC_USER_GETDENTS_ELF}"
        --extra "PRCTL   ELF" "${CXC_USER_ARCH_PRCTL_ELF}"
        --extra "LIBC    ELF" "${CXC_USER_LIBC_ELF}"
        --extra "LIBFS   ELF" "${CXC_USER_LIBC_FS_ELF}"
        --extra "LIBBRK  ELF" "${CXC_USER_LIBC_SBRK_ELF}"
        --extra "LIBMAL  ELF" "${CXC_USER_LIBC_MALLOC_ELF}"
        --extra "LIBERR  ELF" "${CXC_USER_LIBC_ERRNO_ELF}"
        --extra "LIBSTR  ELF" "${CXC_USER_LIBC_STRING_ELF}"
        --extra "LIBWRV  ELF" "${CXC_USER_LIBC_WRITEV_ELF}"
        --extra "LIBCRT  ELF" "${CXC_USER_LIBC_CRT_ELF}"
        --extra "LIBUNA  ELF" "${CXC_USER_LIBC_UNAME_ELF}"
        --extra "LIBTID  ELF" "${CXC_USER_LIBC_TID_ELF}"
        --extra "LIBPID  ELF" "${CXC_USER_LIBC_PID_ELF}"
        --extra "LIBMSL  ELF" "${CXC_USER_MUSL_WRITE_ELF}"
        --extra "LIBMAV  ELF" "${CXC_USER_MUSL_AUXV_ELF}"
        --extra "LIBMCR  ELF" "${CXC_USER_MUSL_CRT_ELF}"
        --extra "LIBMMA  ELF" "${CXC_USER_MUSL_MALLOC_ELF}"
        --extra "LIBMPR  ELF" "${CXC_USER_MUSL_PRINTF_ELF}"
        --extra "LIBFOP  ELF" "${CXC_USER_MUSL_FOPEN_ELF}"
        --extra "LIBODR  ELF" "${CXC_USER_MUSL_OPENDIR_ELF}"
        --extra "LIBUNL  ELF" "${CXC_USER_MUSL_UNLINK_ELF}"
        --extra "LIBBLK  ELF" "${CXC_USER_MUSL_BLKFS_ELF}"
        --extra "LIBDIR  ELF" "${CXC_USER_MUSL_BLKDIR_ELF}"
        --extra "EXSTB   ELF" "${CXC_USER_DISK_EXEC_STUB_ELF}"
        --extra "DEXEC   ELF" "${CXC_USER_DISK_EXEC_PAYLOAD_ELF}"
        --extra "FORK    ELF" "${CXC_USER_FORK_WAIT_ELF}"
        --extra "PIPE    ELF" "${CXC_USER_PIPE_ELF}"
        --extra "CHDIR   ELF" "${CXC_USER_CHDIR_ELF}"
        --extra "NEST    ELF" "${CXC_USER_NEST_ELF}"
        --extra "CAP     ELF" "${CXC_USER_CAP_ELF}"
        --extra "L128S   ELF" "${CXC_USER_LOAD128_STUB_ELF}"
        --extra "L128    ELF" "${CXC_USER_LOAD128_ELF}"
        --extra "HEAP    ELF" "${CXC_USER_HEAP_ELF}"
        --extra "TIME    ELF" "${CXC_USER_TIME_ELF}"
        --extra "ONBXST  ELF" "${CXC_USER_ONBOX_STUB_ELF}"
        --extra "ONBXCC  ELF" "${CXC_USER_ONBOX_CC_ELF}"
        --output "${CXC_ESP_IMAGE}"
    DEPENDS
        cxc_kernel
        "${CXC_USER_EXT_ELF}"
        "${CXC_USER_CROSS_ELF}"
        "${CXC_USER_LSEEK_ELF}"
        "${CXC_USER_FSTAT_ELF}"
        "${CXC_USER_STAT_ELF}"
        "${CXC_USER_GETDENTS_ELF}"
        "${CXC_USER_ARCH_PRCTL_ELF}"
        "${CXC_USER_LIBC_ELF}"
        "${CXC_USER_LIBC_FS_ELF}"
        "${CXC_USER_LIBC_SBRK_ELF}"
        "${CXC_USER_LIBC_MALLOC_ELF}"
        "${CXC_USER_LIBC_ERRNO_ELF}"
        "${CXC_USER_LIBC_STRING_ELF}"
        "${CXC_USER_LIBC_WRITEV_ELF}"
        "${CXC_USER_LIBC_CRT_ELF}"
        "${CXC_USER_LIBC_UNAME_ELF}"
        "${CXC_USER_LIBC_TID_ELF}"
        "${CXC_USER_LIBC_PID_ELF}"
        "${CXC_USER_MUSL_WRITE_ELF}"
        "${CXC_USER_MUSL_AUXV_ELF}"
        "${CXC_USER_MUSL_CRT_ELF}"
        "${CXC_USER_MUSL_MALLOC_ELF}"
        "${CXC_USER_MUSL_PRINTF_ELF}"
        "${CXC_USER_MUSL_FOPEN_ELF}"
        "${CXC_USER_MUSL_OPENDIR_ELF}"
        "${CXC_USER_MUSL_UNLINK_ELF}"
        "${CXC_USER_MUSL_BLKFS_ELF}"
        "${CXC_USER_MUSL_BLKDIR_ELF}"
        "${CXC_USER_DISK_EXEC_STUB_ELF}"
        "${CXC_USER_DISK_EXEC_PAYLOAD_ELF}"
        "${CXC_USER_FORK_WAIT_ELF}"
        "${CXC_USER_PIPE_ELF}"
        "${CXC_USER_CHDIR_ELF}"
        "${CXC_USER_NEST_ELF}"
        "${CXC_USER_CAP_ELF}"
        "${CXC_USER_LOAD128_STUB_ELF}"
        "${CXC_USER_LOAD128_ELF}"
        "${CXC_USER_HEAP_ELF}"
        "${CXC_USER_TIME_ELF}"
        "${CXC_USER_ONBOX_STUB_ELF}"
        "${CXC_USER_ONBOX_CC_ELF}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_esp.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_ext_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cross_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lseek_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fstat_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_stat_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_getdents_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_arch_prctl_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_fs_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_sbrk_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_malloc_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_errno_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_string_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_writev_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_crt_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_uname_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_tid_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_libc_pid_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_write_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_musl_auxv_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_malloc_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_printf_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_fopen_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_opendir_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_unlink_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkfs_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_blkdir_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_execve_stub_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_disk_exec_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fork_wait_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_pipe_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_chdir_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_nest_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cap_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_stub_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_load128_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_heap_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_time_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_onbox_stub_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/lex.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/parse.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/emit.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/prep.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cxc_cc/elflink.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/cxc_cc.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/rtmath.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_rtmath_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/rtmath_elf.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/rtcrt.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_rtcrt_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/rtcrt_elf.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/rtio.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_rtio_elf.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/cxc/rtio_elf.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_crt.ld"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_crt.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_malloc.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_printf.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_fopen.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_opendir.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_unlink.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_blkfs.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/prebuilt/musl_blkdir.elf"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/cross_hello.ld"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/lseek_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/fstat_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/stat_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/getdents_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/arch_prctl_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_fs_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_sbrk_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_malloc_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_errno_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_string_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_writev_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_crt_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_uname_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_tid_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/libc_pid_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_write_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_auxv_probe.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/crt0.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/syscall.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/malloc.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/unistd.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/errno.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/stdlib.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/fcntl.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/string.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/mini_libc/utsname.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/crt0.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/syscall.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/user/musl_subset/unistd.h"
    COMMENT "Creating FAT32 UEFI ESP (+ user ELFs incl. EXSTB/DEXEC.ELF)"
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

