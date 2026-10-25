#pragma once

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

// BSP-only: load the embedded static hello ELF, enter CPL3, run syscall v0
// write+exit, then tear down the user address space.
bool run_elf_probe(ElfProbeResult& result);
const ElfProbeResult& elf_probe_result();

// BSP-only: ramfs open/read/write/close round-trip via embedded FS ELF (M39).
bool run_fs_probe(FsProbeResult& result);
const FsProbeResult& fs_probe_result();

}  // namespace cxc::user
