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

struct LseekProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t lseek_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct FstatProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t fstat_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct StatProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t stat_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct GetdentsProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t getdents_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct ArchPrctlProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t arch_prctl_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct LibcProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t write_calls;
    uint64_t entry;
    uint64_t exit_code;
    uint64_t image_size;
};

struct LibcFsProbeResult {
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
    uint64_t image_size;
};

struct LibcSbrkProbeResult {
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
    uint64_t image_size;
};

struct LibcMallocProbeResult {
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
    uint64_t image_size;
};

struct LibcErrnoProbeResult {
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

struct LibcStringProbeResult {
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

struct LibcWritevProbeResult {
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

struct LibcCrtProbeResult {
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

struct LibcUnameProbeResult {
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

struct LibcTidProbeResult {
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

struct LibcPidProbeResult {
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

struct MuslWriteProbeResult {
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

// BSP-only: host-gcc (or fallback) CROSS.ELF from the ESP (M46).
bool run_cross_elf_probe(const uint8_t* image, size_t image_size,
                         ExtElfProbeResult& result);
const ExtElfProbeResult& cross_elf_probe_result();

// BSP-only: lseek round-trip via ESP LSEEK.ELF (M47).
bool run_lseek_probe(const uint8_t* image, size_t image_size,
                     LseekProbeResult& result);
const LseekProbeResult& lseek_probe_result();

// BSP-only: fstat size/mode via ESP FSTAT.ELF (M48).
bool run_fstat_probe(const uint8_t* image, size_t image_size,
                     FstatProbeResult& result);
const FstatProbeResult& fstat_probe_result();

// BSP-only: path stat on file+dir via ESP STAT.ELF (M49).
bool run_stat_probe(const uint8_t* image, size_t image_size,
                    StatProbeResult& result);
const StatProbeResult& stat_probe_result();

// BSP-only: getdents64 directory listing via ESP DENTS.ELF (M50).
bool run_getdents_probe(const uint8_t* image, size_t image_size,
                        GetdentsProbeResult& result);
const GetdentsProbeResult& getdents_probe_result();

// BSP-only: arch_prctl SET/GET_FS via ESP PRCTL.ELF (M51).
bool run_arch_prctl_probe(const uint8_t* image, size_t image_size,
                         ArchPrctlProbeResult& result);
const ArchPrctlProbeResult& arch_prctl_probe_result();

// BSP-only: mini-libc write/_exit via ESP LIBC.ELF (M52).
bool run_libc_probe(const uint8_t* image, size_t image_size,
                    LibcProbeResult& result);
const LibcProbeResult& libc_probe_result();

// BSP-only: mini-libc open/read/write/close via ESP LIBFS.ELF (M53).
bool run_libc_fs_probe(const uint8_t* image, size_t image_size,
                       LibcFsProbeResult& result);
const LibcFsProbeResult& libc_fs_probe_result();

// BSP-only: mini-libc brk/sbrk via ESP LIBBRK.ELF (M55).
bool run_libc_sbrk_probe(const uint8_t* image, size_t image_size,
                         LibcSbrkProbeResult& result);
const LibcSbrkProbeResult& libc_sbrk_probe_result();

// BSP-only: mini-libc malloc via ESP LIBMAL.ELF (M56).
bool run_libc_malloc_probe(const uint8_t* image, size_t image_size,
                           LibcMallocProbeResult& result);
const LibcMallocProbeResult& libc_malloc_probe_result();

// BSP-only: mini-libc errno via ESP LIBERR.ELF (M57).
bool run_libc_errno_probe(const uint8_t* image, size_t image_size,
                          LibcErrnoProbeResult& result);
const LibcErrnoProbeResult& libc_errno_probe_result();

// BSP-only: mini-libc string via ESP LIBSTR.ELF (M58).
bool run_libc_string_probe(const uint8_t* image, size_t image_size,
                           LibcStringProbeResult& result);
const LibcStringProbeResult& libc_string_probe_result();

// BSP-only: mini-libc writev via ESP LIBWRV.ELF (M59).
bool run_libc_writev_probe(const uint8_t* image, size_t image_size,
                           LibcWritevProbeResult& result);
const LibcWritevProbeResult& libc_writev_probe_result();

// BSP-only: mini-libc crt0→main via ESP LIBCRT.ELF (M60).
bool run_libc_crt_probe(const uint8_t* image, size_t image_size,
                        LibcCrtProbeResult& result);
const LibcCrtProbeResult& libc_crt_probe_result();

// BSP-only: mini-libc uname via ESP LIBUNA.ELF (M61).
bool run_libc_uname_probe(const uint8_t* image, size_t image_size,
                          LibcUnameProbeResult& result);
const LibcUnameProbeResult& libc_uname_probe_result();

// BSP-only: mini-libc set_tid_address via ESP LIBTID.ELF (M62).
bool run_libc_tid_probe(const uint8_t* image, size_t image_size,
                        LibcTidProbeResult& result);
const LibcTidProbeResult& libc_tid_probe_result();

// BSP-only: mini-libc getpid/getppid via ESP LIBPID.ELF (M63).
bool run_libc_pid_probe(const uint8_t* image, size_t image_size,
                        LibcPidProbeResult& result);
const LibcPidProbeResult& libc_pid_probe_result();

// BSP-only: musl-subset write via ESP LIBMSL.ELF (M65).
bool run_musl_write_probe(const uint8_t* image, size_t image_size,
                          MuslWriteProbeResult& result);
const MuslWriteProbeResult& musl_write_probe_result();

}  // namespace cxc::user
