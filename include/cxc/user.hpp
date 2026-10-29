#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::cpu {
struct InterruptFrame;
}

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

struct MuslAuxvProbeResult {
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

struct MuslCrtProbeResult {
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

struct MuslMallocProbeResult {
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

struct MuslPrintfProbeResult {
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

struct MuslFopenProbeResult {
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

struct MuslOpendirProbeResult {
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

struct MuslUnlinkProbeResult {
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

// BSP-only: musl-subset stack argc/argv via ESP LIBMAV.ELF (M67).
bool run_musl_auxv_probe(const uint8_t* image, size_t image_size,
                         MuslAuxvProbeResult& result);
const MuslAuxvProbeResult& musl_auxv_probe_result();

// BSP-only: real musl-gcc static CRT via ESP LIBMCR.ELF (M68).
bool run_musl_crt_probe(const uint8_t* image, size_t image_size,
                        MuslCrtProbeResult& result);
const MuslCrtProbeResult& musl_crt_probe_result();

// BSP-only: musl-gcc malloc/free via ESP LIBMMA.ELF (M69).
bool run_musl_malloc_probe(const uint8_t* image, size_t image_size,
                           MuslMallocProbeResult& result);
const MuslMallocProbeResult& musl_malloc_probe_result();

// BSP-only: musl-gcc printf via ESP LIBMPR.ELF (M70).
bool run_musl_printf_probe(const uint8_t* image, size_t image_size,
                           MuslPrintfProbeResult& result);
const MuslPrintfProbeResult& musl_printf_probe_result();

// BSP-only: musl-gcc fopen via ESP LIBFOP.ELF (M71).
bool run_musl_fopen_probe(const uint8_t* image, size_t image_size,
                          MuslFopenProbeResult& result);
const MuslFopenProbeResult& musl_fopen_probe_result();

// BSP-only: musl-gcc opendir/readdir via ESP LIBODR.ELF (M72).
bool run_musl_opendir_probe(const uint8_t* image, size_t image_size,
                            MuslOpendirProbeResult& result);
const MuslOpendirProbeResult& musl_opendir_probe_result();

// BSP-only: musl-gcc unlink via ESP LIBUNL.ELF (M73).
bool run_musl_unlink_probe(const uint8_t* image, size_t image_size,
                           MuslUnlinkProbeResult& result);
const MuslUnlinkProbeResult& musl_unlink_probe_result();

struct MuslBlkfsProbeResult {
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

bool run_musl_blkfs_probe(const uint8_t* image, size_t image_size,
                          MuslBlkfsProbeResult& result);
const MuslBlkfsProbeResult& musl_blkfs_probe_result();

struct MuslBlkdirProbeResult {
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

bool run_musl_blkdir_probe(const uint8_t* image, size_t image_size,
                           MuslBlkdirProbeResult& result);
const MuslBlkdirProbeResult& musl_blkdir_probe_result();

struct DiskExecProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t wrote_elf;
    uint32_t remount_ok;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t argv_pass;
    uint32_t env_pass;
    uint32_t elf_bytes;
    uint64_t exit_code;
};

// M80–M82: write payload ELF to disk/run.elf; stub execve's with argv/envp.
bool run_disk_exec_probe(const uint8_t* stub_image, size_t stub_size,
                         const uint8_t* payload_image, size_t payload_size,
                         DiskExecProbeResult& result);
const DiskExecProbeResult& disk_exec_probe_result();

struct ForkWaitProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M83: launch FORK.ELF — fork + wait4 + ECHILD edge.
bool run_fork_wait_probe(const uint8_t* image, size_t image_size,
                         ForkWaitProbeResult& result);
const ForkWaitProbeResult& fork_wait_probe_result();

struct PipeProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M85: launch PIPE.ELF — pipe + fork IPC + dup + EPIPE edge.
bool run_pipe_probe(const uint8_t* image, size_t image_size,
                    PipeProbeResult& result);
const PipeProbeResult& pipe_probe_result();

struct ChdirProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M86: launch CHDIR.ELF — chdir/getcwd + relative open + edges.
bool run_chdir_probe(const uint8_t* image, size_t image_size,
                     ChdirProbeResult& result);
const ChdirProbeResult& chdir_probe_result();

struct NestProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M87: launch NEST.ELF — nested mkdir on disk/ + relative creat + orphan edge.
bool run_nest_probe(const uint8_t* image, size_t image_size,
                    NestProbeResult& result);
const NestProbeResult& nest_probe_result();

struct CapProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M88: launch CAP.ELF — 96 KiB CXCFS write/read (128 KiB ceiling).
bool run_cap_probe(const uint8_t* image, size_t image_size,
                   CapProbeResult& result);
const CapProbeResult& cap_probe_result();

struct Load128ProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t wrote_elf;
    uint32_t remount_ok;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t elf_bytes;
    uint64_t exit_code;
};

// M89: write >64 KiB ET_EXEC to disk/, execve via stub → LOAD128_PASS.
bool run_load128_probe(const uint8_t* stub_image, size_t stub_size,
                       const uint8_t* payload_image, size_t payload_size,
                       Load128ProbeResult& result);
const Load128ProbeResult& load128_probe_result();

struct HeapProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M90: launch HEAP.ELF — brk 768 KiB + mmap 512 KiB (1 MiB ceilings).
bool run_heap_probe(const uint8_t* image, size_t image_size,
                    HeapProbeResult& result);
const HeapProbeResult& heap_probe_result();

struct TimeProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint64_t exit_code;
};

// M91: launch TIME.ELF — access + clock_gettime + gettimeofday.
bool run_time_probe(const uint8_t* image, size_t image_size,
                    TimeProbeResult& result);
const TimeProbeResult& time_probe_result();

struct OnboxCcProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t wrote_src;
    uint32_t wrote_cc;
    uint32_t remount_ok;
    uint32_t loaded;
    uint32_t entered;
    uint32_t exited;
    uint32_t wrote_pass;
    uint32_t edge_ok;
    uint32_t fat_ok;     // M260: >1 MiB static ELF execve → 42
    uint32_t as_ok;      // M261/M266: external as wrote ET_REL
    uint32_t bridge_ok;  // M262: disk/tcap_br.o ET_REL verified on disk
    uint32_t tcap;       // M264: fat+as+bridge all evidence-backed
    uint32_t spawn_as;   // HARD-session execve("disk/as") from kernel audit
    uint32_t spawn_ld;   // HARD-session execve("disk/ld") from kernel audit
    uint32_t spawn_cc;   // HARD-session execve("disk/cc"|"disk/cxc-cc")
    uint32_t inputs_c;   // M293: .c inputs in job.rsp (spawn_cc == 1+inputs_c)
    uint32_t inputs_s;   // M295: .s inputs in job.rsp (spawn_as == inputs_s)
    uint32_t planted;    // M295: remaining host-prebuilt ET_REL leaves in job.rsp
    uint32_t tdrv;       // M274 kept: as/ld driver evidence subset
    uint32_t ccdrv;      // M284: disk/cc argv driver + multi-.c evidence
    uint64_t exit_code;
    uint32_t fat_bytes;
};

// M284/CCDRV: disk/cc argv CLI → nested -c + as/ld → ONBOX_CCDRV_PASS.
bool run_onbox_cc_probe(const uint8_t* stub_image, size_t stub_size,
                        const uint8_t* cc_image, size_t cc_size,
                        OnboxCcProbeResult& result);
const OnboxCcProbeResult& onbox_cc_probe_result();

struct IdeRebuildResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t saved;
    uint32_t built;
    uint32_t ran;
    uint64_t exit_code;
    uint32_t src_bytes;  // disk/ide_main.c after save
    uint32_t out_bytes;  // disk/ide_out.elf after build (0 if missing)
};

// Mini-IDE helpers (after CCDRV so disk/cc + as/ld exist).
bool ide_save_source(const char* source, size_t source_len, uint32_t& failure);
// Save to an arbitrary disk/ path (NUL-terminated).
bool ide_save_path(const char* path, const char* source, size_t source_len,
                   uint32_t& failure);
// Lightweight: disk/cc -o disk/ide_out.elf <src_path> (default disk/ide_main.c).
bool ide_build_and_run(const uint8_t* cc_image, size_t cc_size,
                       IdeRebuildResult& result,
                       const char* src_path = nullptr);
bool ide_run_out(IdeRebuildResult& result);
// Save + build+run convenience for the automated contract.
bool run_ide_rebuild(const char* source, size_t source_len,
                     const uint8_t* cc_image, size_t cc_size,
                     IdeRebuildResult& result);

// M259: long argv/env for real toolchain drivers.
constexpr uint32_t max_exec_argc = 64;
constexpr uint32_t max_exec_envc = 64;
constexpr size_t max_exec_arg_bytes = 1025;  // holds ≥1024 payload + NUL

// Replace the current user image (syscall execve). Updates `frame` for iret.
// `argv` may be null/empty → seed argc=1 argv[0]="musl" (launch default).
// `envp` may be null/empty → empty environment (NULL terminator only).
bool exec_replace(const char* path, cpu::InterruptFrame* frame,
                  uint32_t& failure, const char* const* argv = nullptr,
                  uint32_t argc = 0, const char* const* envp = nullptr,
                  uint32_t envc = 0);

}  // namespace cxc::user
