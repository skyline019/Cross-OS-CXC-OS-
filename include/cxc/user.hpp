#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::user {

struct ElfProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t entry;
    uint64_t exit_code;
};

struct FsProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t opens;
    uint32_t reads;
    uint32_t writes;
    uint32_t closes;
    uint64_t entry;
    uint64_t exit_code;
};

struct BrkProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t grows;
    uint32_t pages;
    uint64_t entry;
    uint64_t exit_code;
};

struct MmapProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t calls;
    uint32_t pages;
    uint64_t entry;
    uint64_t exit_code;
};

struct DirProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t mkdirs;
    uint32_t opens;
    uint32_t dirs;
    uint64_t entry;
    uint64_t exit_code;
};

struct MunmapProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t mmap_calls;
    uint32_t munmap_calls;
    uint64_t entry;
    uint64_t exit_code;
};

struct ExtElfProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

// BSP-only: load the embedded static hello ELF, enter CPL3, run syscall v0
// write+exit, then tear down the user address space.
bool run_elf_probe(ElfProbeResult& result);
const ElfProbeResult& elf_probe_result();

// BSP-only: ramfs open/read/write/close round-trip via embedded FS ELF (M39).
bool run_fs_probe(FsProbeResult& result);
const FsProbeResult& fs_probe_result();

// BSP-only: brk grow + touch via embedded ELF (M40).
bool run_brk_probe(BrkProbeResult& result);
const BrkProbeResult& brk_probe_result();

// BSP-only: anonymous mmap + touch via embedded ELF (M41).
bool run_mmap_probe(MmapProbeResult& result);
const MmapProbeResult& mmap_probe_result();

// BSP-only: mkdir + nested path open/read/write (M42).
bool run_dir_probe(DirProbeResult& result);
const DirProbeResult& dir_probe_result();

// BSP-only: mmap + munmap round-trip via embedded ELF (M43).
bool run_munmap_probe(MunmapProbeResult& result);
const MunmapProbeResult& munmap_probe_result();

// BSP-only: load a non-embedded static ELF stashed from the ESP (M45).
bool run_ext_elf_probe(const uint8_t* image, size_t image_size,
                       ExtElfProbeResult& result);
const ExtElfProbeResult& ext_elf_probe_result();

}  // namespace cxc::user
