#include <cxc/user.hpp>
#include <cxc/plant_asm_texts.hpp>
#include <cxc/host_add_elf.hpp>
#include <cxc/host_bump_elf.hpp>
#include <cxc/musl_memcpy_elf.hpp>
#include <cxc/musl_strcspn_elf.hpp>
#include <cxc/musl_strspn_elf.hpp>
#include <cxc/musl_strpbrk_elf.hpp>
#include <cxc/musl_strrchr_elf.hpp>
#include <cxc/musl_memmove_elf.hpp>
#include <cxc/musl_strdup_elf.hpp>
#include <cxc/musl_strtol_elf.hpp>
#include <cxc/musl_intscan_elf.hpp>
#include <cxc/musl_shgetc_elf.hpp>
#include <cxc/musl_memrchr_elf.hpp>
#include <cxc/hard_stubs_elf.hpp>
#include <cxc/fat42_elf.hpp>
#include <cxc/fat_stub_elf.hpp>
#include <cxc/mini_as_elf.hpp>
#include <cxc/as_cli_elf.hpp>
#include <cxc/ld_cli_elf.hpp>
#include <cxc/as_stub_elf.hpp>
#include <cxc/tcap_bridge_elf.hpp>
#include <cxc/rtcrt_elf.hpp>
#include <cxc/rtio_elf.hpp>
#include <cxc/rtmath_elf.hpp>

#include <cxc/cpu.hpp>
#include <cxc/cxcfs.hpp>
#include <cxc/elf.hpp>
#include <cxc/fs.hpp>
#include <cxc/pmm.hpp>
#include <cxc/smp.hpp>
#include <cxc/syscall.hpp>
#include <cxc/user_brk_blob.hpp>
#include <cxc/user_dir_blob.hpp>
#include <cxc/user_fs_blob.hpp>
#include <cxc/user_hello_blob.hpp>
#include <cxc/user_mmap_blob.hpp>
#include <cxc/user_munmap_blob.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void ring3_enter_iretq(uint64_t user_rip, uint64_t user_rsp);
extern "C" void ring3_after_user();
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::user {
namespace {

// Linux auxv keys (subset used by musl __init_libc / static CRT).
constexpr uint64_t at_null = 0;
constexpr uint64_t at_phdr = 3;
constexpr uint64_t at_phent = 4;
constexpr uint64_t at_phnum = 5;
constexpr uint64_t at_pagesz = 6;
constexpr uint64_t at_base = 7;
constexpr uint64_t at_entry = 9;
constexpr uint64_t at_uid = 11;
constexpr uint64_t at_euid = 12;
constexpr uint64_t at_gid = 13;
constexpr uint64_t at_egid = 14;
constexpr uint64_t at_secure = 23;
constexpr uint64_t at_random = 25;

// M67/M81/M82: seed argc/argv/envp/auxv at the top of the user stack page so
// musl `_start` (`mov %rsp,%rdi`) can read a Linux-shaped process image.
uint64_t seed_linux_user_stack(uint8_t* stack_base, uint64_t stack_va_base,
                               uint64_t stack_pages, uint64_t entry,
                               uint64_t phdr_va, uint64_t phent, uint64_t phnum,
                               const char* const* argv, uint32_t argc,
                               const char* const* envp, uint32_t envc) {
    const size_t stack_bytes =
        static_cast<size_t>(stack_pages) * pmm::page_size;
    memset(stack_base, 0, stack_bytes);
    uint8_t* cursor = stack_base + stack_bytes;

    auto push_bytes = [&](const void* src, size_t length) -> uint64_t {
        cursor -= length;
        memcpy(cursor, src, length);
        return stack_va_base + static_cast<uint64_t>(cursor - stack_base);
    };

    static const char default_arg0[] = "musl";
    const char* local_argv[1] = {default_arg0};
    if (argv == nullptr || argc == 0) {
        argv = local_argv;
        argc = 1;
    }
    if (argc > max_exec_argc) {
        argc = max_exec_argc;
    }
    if (envp == nullptr) {
        envc = 0;
    }
    if (envc > max_exec_envc) {
        envc = max_exec_envc;
    }

    uint64_t arg_vas[max_exec_argc]{};
    for (uint32_t index = 0; index < argc; ++index) {
        const char* text = argv[index] != nullptr ? argv[index] : "";
        size_t length = 0;
        while (length < max_exec_arg_bytes - 1 && text[length] != '\0') {
            ++length;
        }
        arg_vas[index] = push_bytes(text, length + 1);
    }

    uint64_t env_vas[max_exec_envc]{};
    for (uint32_t index = 0; index < envc; ++index) {
        const char* text = envp[index] != nullptr ? envp[index] : "";
        size_t length = 0;
        while (length < max_exec_arg_bytes - 1 && text[length] != '\0') {
            ++length;
        }
        env_vas[index] = push_bytes(text, length + 1);
    }

    uint8_t random[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint64_t random_va = push_bytes(random, sizeof(random));

    // Align vector area so final RSP is 16-byte aligned (SysV AMD64).
    cursor = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(cursor) & ~uintptr_t{0xfull});

    auto push_u64 = [&](uint64_t value) {
        cursor -= 8;
        memcpy(cursor, &value, 8);
    };

    // Highest: auxv pairs (AT_NULL last), then envp, argv, argc.
    push_u64(0);
    push_u64(at_null);
    push_u64(random_va);
    push_u64(at_random);
    push_u64(0);
    push_u64(at_secure);
    push_u64(0);
    push_u64(at_egid);
    push_u64(0);
    push_u64(at_gid);
    push_u64(0);
    push_u64(at_euid);
    push_u64(0);
    push_u64(at_uid);
    push_u64(entry);
    push_u64(at_entry);
    push_u64(0);
    push_u64(at_base);
    push_u64(pmm::page_size);
    push_u64(at_pagesz);
    push_u64(phnum);
    push_u64(at_phnum);
    push_u64(phent);
    push_u64(at_phent);
    push_u64(phdr_va);
    push_u64(at_phdr);

    push_u64(0);  // envp terminator
    for (uint32_t index = envc; index > 0; --index) {
        push_u64(env_vas[index - 1]);
    }
    push_u64(0);  // argv terminator
    for (uint32_t index = argc; index > 0; --index) {
        push_u64(arg_vas[index - 1]);
    }
    push_u64(argc);

    return stack_va_base + static_cast<uint64_t>(cursor - stack_base);
}

ElfProbeResult elf_probe{};
FsProbeResult fs_probe{};
BrkProbeResult brk_probe{};
MmapProbeResult mmap_probe{};
DirProbeResult dir_probe{};
MunmapProbeResult munmap_probe{};
ExtElfProbeResult ext_elf_probe{};
ExtElfProbeResult cross_elf_probe{};
LseekProbeResult lseek_probe{};
FstatProbeResult fstat_probe{};
StatProbeResult stat_probe{};
GetdentsProbeResult getdents_probe{};
ArchPrctlProbeResult arch_prctl_probe{};
LibcProbeResult libc_probe{};
LibcFsProbeResult libc_fs_probe{};
LibcSbrkProbeResult libc_sbrk_probe{};
LibcMallocProbeResult libc_malloc_probe{};
LibcErrnoProbeResult libc_errno_probe{};
LibcStringProbeResult libc_string_probe{};
LibcWritevProbeResult libc_writev_probe{};
LibcCrtProbeResult libc_crt_probe{};
LibcUnameProbeResult libc_uname_probe{};
LibcTidProbeResult libc_tid_probe{};
LibcPidProbeResult libc_pid_probe{};
MuslWriteProbeResult musl_write_probe{};
MuslAuxvProbeResult musl_auxv_probe{};
MuslCrtProbeResult musl_crt_probe{};
MuslMallocProbeResult musl_malloc_probe{};
MuslPrintfProbeResult musl_printf_probe{};
MuslFopenProbeResult musl_fopen_probe{};
MuslOpendirProbeResult musl_opendir_probe{};
MuslUnlinkProbeResult musl_unlink_probe{};
MuslBlkfsProbeResult musl_blkfs_probe{};
MuslBlkdirProbeResult musl_blkdir_probe{};
DiskExecProbeResult disk_exec_probe{};
ForkWaitProbeResult fork_wait_probe{};
PipeProbeResult pipe_probe{};
ChdirProbeResult chdir_probe{};
NestProbeResult nest_probe{};
CapProbeResult cap_probe{};
Load128ProbeResult load128_probe{};
HeapProbeResult heap_probe{};
TimeProbeResult time_probe{};
OnboxCcProbeResult onbox_cc_probe{};
/* M257: above brk (≤0x1409000) and mmap (≤0x3000000) windows. */
constexpr uint64_t user_stack_base = 0x0000000003800000ull;
// M259: long argv/env needs >64 KiB; keep headroom for mini-cc frames.
constexpr uint64_t user_stack_pages = 64;
constexpr uint64_t privilege_stack_base = 0xffffffff5e000000ull;

bool map_user_stack(uint64_t user_root, uint64_t& stack_physical,
                    uint64_t& top_page_va) {
    stack_physical = pmm::allocate_pages(user_stack_pages);
    if (stack_physical == 0) {
        return false;
    }
    for (uint64_t i = 0; i < user_stack_pages; ++i) {
        const uint64_t va = user_stack_base + i * pmm::page_size;
        const uint64_t pa = stack_physical + i * pmm::page_size;
        if (!vmm::map_user_page(user_root, va, pa, vmm::user_map_writable)) {
            pmm::free_pages(stack_physical, user_stack_pages);
            stack_physical = 0;
            return false;
        }
    }
    top_page_va =
        user_stack_base + (user_stack_pages - 1) * pmm::page_size;
    return true;
}
constexpr uint64_t privilege_stack_pages = 8;

bool ensure_tss() {
    const uint64_t stack_top =
        privilege_stack_base + privilege_stack_pages * pmm::page_size;
    return cpu::initialize_tss(stack_top);
}

bool launch_user_elf(const uint8_t* image, size_t image_size, uint64_t& entry,
                     uint32_t& stage, uint32_t& failure,
                     const char* const* argv = nullptr, uint32_t argc = 0) {
    stage = 1;
    if (smp::current_cpu_index() != 0 || vmm::kernel_address_space() == 0) {
        failure = 1;
        return false;
    }
    if (!ensure_tss()) {
        failure = 2;
        return false;
    }
    stage = 2;

    const uint64_t user_root = vmm::create_user_address_space();
    if (user_root == 0) {
        failure = 3;
        return false;
    }

    elf::LoadResult loaded{};
    if (!elf::load_static_et_exec(user_root, image, image_size, loaded)) {
        (void)vmm::destroy_user_address_space(user_root);
        failure = 10 + loaded.failure;
        return false;
    }
    entry = loaded.entry;
    stage = 3;

    uint64_t stack_physical = 0;
    uint64_t top_page_va = 0;
    if (!map_user_stack(user_root, stack_physical, top_page_va)) {
        (void)vmm::destroy_user_address_space(user_root);
        failure = 4;
        return false;
    }

    const uint64_t kernel_root = vmm::kernel_address_space();
    (void)top_page_va;
    auto* const stack_base = reinterpret_cast<uint8_t*>(
        vmm::direct_map_base + stack_physical);
    const uint64_t user_rsp = seed_linux_user_stack(
        stack_base, user_stack_base, user_stack_pages, loaded.entry,
        loaded.at_phdr, loaded.at_phent, loaded.at_phnum, argv, argc, nullptr,
        0);
    syscall::begin_session(reinterpret_cast<uint64_t>(&ring3_after_user));
    syscall::set_user_root(user_root);

    const uint64_t irq_flags = cpu::read_flags();
    cpu::disable_interrupts();
    if (!vmm::activate_address_space(user_root)) {
        cpu::restore_interrupts(irq_flags);
        syscall::clear_session();
        (void)vmm::destroy_user_address_space(user_root);
        failure = 5;
        return false;
    }

    ring3_enter_iretq(loaded.entry, user_rsp);

    if (!vmm::activate_address_space(kernel_root)) {
        cpu::restore_interrupts(irq_flags);
        failure = 6;
        return false;
    }
    cpu::restore_interrupts(irq_flags);
    stage = 4;

    // execve may have replaced the address space; tear down the live root.
    const uint64_t live_root = syscall::session().user_root != 0
                                   ? syscall::session().user_root
                                   : user_root;

    if (syscall::session().exited == 0) {
        syscall::clear_session();
        (void)vmm::destroy_user_address_space(live_root);
        failure = 7;
        return false;
    }
    stage = 5;

    if (!vmm::destroy_user_address_space(live_root)) {
        syscall::clear_session();
        failure = 9;
        return false;
    }
    syscall::set_user_root(0);
    stage = 6;
    return true;
}

}  // namespace

bool run_elf_probe(ElfProbeResult& result) {
    result = ElfProbeResult{};
    if (!launch_user_elf(hello_elf, hello_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_program_pass;
    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        syscall::session().write_calls == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    elf_probe = result;
    return true;
}

const ElfProbeResult& elf_probe_result() {
    return elf_probe;
}

bool run_fs_probe(FsProbeResult& result) {
    result = FsProbeResult{};
    if (!launch_user_elf(fs_elf, fs_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_fs_pass;
    const fs::Stats& st = fs::stats();
    result.opens = st.opens;
    result.reads = st.reads;
    result.writes = st.writes;
    result.closes = st.closes;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.opens < 2 ||
        result.reads < 1 || result.writes < 1 || result.closes < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    fs_probe = result;
    return true;
}

const FsProbeResult& fs_probe_result() {
    return fs_probe;
}

bool run_brk_probe(BrkProbeResult& result) {
    result = BrkProbeResult{};
    if (!launch_user_elf(brk_elf, brk_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_brk_pass;
    result.grows = syscall::session().brk_grows;
    result.pages = syscall::session().brk_pages;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.grows < 1 ||
        result.pages < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    brk_probe = result;
    return true;
}

const BrkProbeResult& brk_probe_result() {
    return brk_probe;
}

bool run_mmap_probe(MmapProbeResult& result) {
    result = MmapProbeResult{};
    if (!launch_user_elf(mmap_elf, mmap_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_mmap_pass;
    result.calls = syscall::session().mmap_calls;
    result.pages = syscall::session().mmap_pages;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.calls < 1 ||
        result.pages < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    mmap_probe = result;
    return true;
}

const MmapProbeResult& mmap_probe_result() {
    return mmap_probe;
}

bool run_dir_probe(DirProbeResult& result) {
    result = DirProbeResult{};
    if (!launch_user_elf(dir_elf, dir_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_dir_pass;
    const fs::Stats& st = fs::stats();
    result.mkdirs = st.mkdirs;
    result.opens = st.opens;
    result.dirs = st.dirs;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.mkdirs < 1 ||
        result.opens < 2 || result.dirs < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    dir_probe = result;
    return true;
}

const DirProbeResult& dir_probe_result() {
    return dir_probe;
}

bool run_munmap_probe(MunmapProbeResult& result) {
    result = MunmapProbeResult{};
    if (!launch_user_elf(munmap_elf, munmap_elf_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_munmap_pass;
    result.mmap_calls = syscall::session().mmap_calls;
    result.munmap_calls = syscall::session().munmap_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.mmap_calls < 1 || result.munmap_calls < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    munmap_probe = result;
    return true;
}

const MunmapProbeResult& munmap_probe_result() {
    return munmap_probe;
}

bool run_ext_elf_probe(const uint8_t* image, size_t image_size,
                       ExtElfProbeResult& result) {
    result = ExtElfProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_ext_elf_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    ext_elf_probe = result;
    return true;
}

const ExtElfProbeResult& ext_elf_probe_result() {
    return ext_elf_probe;
}

bool run_cross_elf_probe(const uint8_t* image, size_t image_size,
                         ExtElfProbeResult& result) {
    result = ExtElfProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_cross_elf_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    cross_elf_probe = result;
    return true;
}

const ExtElfProbeResult& cross_elf_probe_result() {
    return cross_elf_probe;
}

bool run_lseek_probe(const uint8_t* image, size_t image_size,
                     LseekProbeResult& result) {
    result = LseekProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_lseek_pass;
    result.lseek_calls = syscall::session().lseek_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.lseek_calls < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    lseek_probe = result;
    return true;
}

const LseekProbeResult& lseek_probe_result() {
    return lseek_probe;
}

bool run_fstat_probe(const uint8_t* image, size_t image_size,
                     FstatProbeResult& result) {
    result = FstatProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_fstat_pass;
    result.fstat_calls = syscall::session().fstat_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.fstat_calls < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    fstat_probe = result;
    return true;
}

const FstatProbeResult& fstat_probe_result() {
    return fstat_probe;
}

bool run_stat_probe(const uint8_t* image, size_t image_size,
                    StatProbeResult& result) {
    result = StatProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_stat_pass;
    result.stat_calls = syscall::session().stat_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.stat_calls < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    stat_probe = result;
    return true;
}

const StatProbeResult& stat_probe_result() {
    return stat_probe;
}

bool run_getdents_probe(const uint8_t* image, size_t image_size,
                        GetdentsProbeResult& result) {
    result = GetdentsProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_getdents_pass;
    result.getdents_calls = syscall::session().getdents_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.getdents_calls < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    getdents_probe = result;
    return true;
}

const GetdentsProbeResult& getdents_probe_result() {
    return getdents_probe;
}

bool run_arch_prctl_probe(const uint8_t* image, size_t image_size,
                          ArchPrctlProbeResult& result) {
    result = ArchPrctlProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_arch_prctl_pass;
    result.arch_prctl_calls = syscall::session().arch_prctl_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.arch_prctl_calls < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    arch_prctl_probe = result;
    return true;
}

const ArchPrctlProbeResult& arch_prctl_probe_result() {
    return arch_prctl_probe;
}

bool run_libc_probe(const uint8_t* image, size_t image_size,
                    LibcProbeResult& result) {
    result = LibcProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_pass;
    result.write_calls = syscall::session().write_calls;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.write_calls < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_probe = result;
    return true;
}

const LibcProbeResult& libc_probe_result() {
    return libc_probe;
}

bool run_libc_fs_probe(const uint8_t* image, size_t image_size,
                       LibcFsProbeResult& result) {
    result = LibcFsProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_fs_pass;
    const fs::Stats& st = fs::stats();
    result.opens = st.opens;
    result.reads = st.reads;
    result.writes = st.writes;
    result.closes = st.closes;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.opens < 2 ||
        result.reads < 1 || result.writes < 1 || result.closes < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_fs_probe = result;
    return true;
}

const LibcFsProbeResult& libc_fs_probe_result() {
    return libc_fs_probe;
}

bool run_libc_sbrk_probe(const uint8_t* image, size_t image_size,
                         LibcSbrkProbeResult& result) {
    result = LibcSbrkProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_sbrk_pass;
    result.grows = syscall::session().brk_grows;
    result.pages = syscall::session().brk_pages;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.grows < 1 ||
        result.pages < 1) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_sbrk_probe = result;
    return true;
}

const LibcSbrkProbeResult& libc_sbrk_probe_result() {
    return libc_sbrk_probe;
}

bool run_libc_malloc_probe(const uint8_t* image, size_t image_size,
                           LibcMallocProbeResult& result) {
    result = LibcMallocProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_malloc_pass;
    result.grows = syscall::session().brk_grows;
    result.pages = syscall::session().brk_pages;

    if (result.exit_code != 0 || result.wrote_pass == 0 || result.grows < 2 ||
        result.pages < 2) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_malloc_probe = result;
    return true;
}

const LibcMallocProbeResult& libc_malloc_probe_result() {
    return libc_malloc_probe;
}

bool run_libc_errno_probe(const uint8_t* image, size_t image_size,
                          LibcErrnoProbeResult& result) {
    result = LibcErrnoProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_errno_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_errno_probe = result;
    return true;
}

const LibcErrnoProbeResult& libc_errno_probe_result() {
    return libc_errno_probe;
}

bool run_libc_string_probe(const uint8_t* image, size_t image_size,
                           LibcStringProbeResult& result) {
    result = LibcStringProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_string_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_string_probe = result;
    return true;
}

const LibcStringProbeResult& libc_string_probe_result() {
    return libc_string_probe;
}

bool run_libc_writev_probe(const uint8_t* image, size_t image_size,
                           LibcWritevProbeResult& result) {
    result = LibcWritevProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_writev_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_writev_probe = result;
    return true;
}

const LibcWritevProbeResult& libc_writev_probe_result() {
    return libc_writev_probe;
}

bool run_libc_crt_probe(const uint8_t* image, size_t image_size,
                        LibcCrtProbeResult& result) {
    result = LibcCrtProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_crt_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_crt_probe = result;
    return true;
}

const LibcCrtProbeResult& libc_crt_probe_result() {
    return libc_crt_probe;
}

bool run_libc_uname_probe(const uint8_t* image, size_t image_size,
                          LibcUnameProbeResult& result) {
    result = LibcUnameProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_uname_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_uname_probe = result;
    return true;
}

const LibcUnameProbeResult& libc_uname_probe_result() {
    return libc_uname_probe;
}

bool run_libc_tid_probe(const uint8_t* image, size_t image_size,
                        LibcTidProbeResult& result) {
    result = LibcTidProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_tid_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_tid_probe = result;
    return true;
}

const LibcTidProbeResult& libc_tid_probe_result() {
    return libc_tid_probe;
}

bool run_libc_pid_probe(const uint8_t* image, size_t image_size,
                        LibcPidProbeResult& result) {
    result = LibcPidProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_libc_pid_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    libc_pid_probe = result;
    return true;
}

const LibcPidProbeResult& libc_pid_probe_result() {
    return libc_pid_probe;
}

bool run_musl_write_probe(const uint8_t* image, size_t image_size,
                          MuslWriteProbeResult& result) {
    result = MuslWriteProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_write_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_write_probe = result;
    return true;
}

const MuslWriteProbeResult& musl_write_probe_result() {
    return musl_write_probe;
}

bool run_musl_auxv_probe(const uint8_t* image, size_t image_size,
                         MuslAuxvProbeResult& result) {
    result = MuslAuxvProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_auxv_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_auxv_probe = result;
    return true;
}

const MuslAuxvProbeResult& musl_auxv_probe_result() {
    return musl_auxv_probe;
}

bool run_musl_crt_probe(const uint8_t* image, size_t image_size,
                        MuslCrtProbeResult& result) {
    result = MuslCrtProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_crt_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_crt_probe = result;
    return true;
}

const MuslCrtProbeResult& musl_crt_probe_result() {
    return musl_crt_probe;
}

bool run_musl_malloc_probe(const uint8_t* image, size_t image_size,
                           MuslMallocProbeResult& result) {
    result = MuslMallocProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_malloc_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_malloc_probe = result;
    return true;
}

const MuslMallocProbeResult& musl_malloc_probe_result() {
    return musl_malloc_probe;
}

bool run_musl_printf_probe(const uint8_t* image, size_t image_size,
                           MuslPrintfProbeResult& result) {
    result = MuslPrintfProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_printf_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_printf_probe = result;
    return true;
}

const MuslPrintfProbeResult& musl_printf_probe_result() {
    return musl_printf_probe;
}

bool run_musl_fopen_probe(const uint8_t* image, size_t image_size,
                          MuslFopenProbeResult& result) {
    result = MuslFopenProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_fopen_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_fopen_probe = result;
    return true;
}

const MuslFopenProbeResult& musl_fopen_probe_result() {
    return musl_fopen_probe;
}

bool run_musl_opendir_probe(const uint8_t* image, size_t image_size,
                            MuslOpendirProbeResult& result) {
    result = MuslOpendirProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_opendir_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_opendir_probe = result;
    return true;
}

const MuslOpendirProbeResult& musl_opendir_probe_result() {
    return musl_opendir_probe;
}

bool run_musl_unlink_probe(const uint8_t* image, size_t image_size,
                           MuslUnlinkProbeResult& result) {
    result = MuslUnlinkProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_unlink_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_unlink_probe = result;
    return true;
}

const MuslUnlinkProbeResult& musl_unlink_probe_result() {
    return musl_unlink_probe;
}

bool run_musl_blkfs_probe(const uint8_t* image, size_t image_size,
                          MuslBlkfsProbeResult& result) {
    result = MuslBlkfsProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_blk_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_blkfs_probe = result;
    return true;
}

const MuslBlkfsProbeResult& musl_blkfs_probe_result() {
    return musl_blkfs_probe;
}

bool run_musl_blkdir_probe(const uint8_t* image, size_t image_size,
                           MuslBlkdirProbeResult& result) {
    result = MuslBlkdirProbeResult{};
    result.image_size = image_size;
    if (image == nullptr || image_size == 0) {
        result.failure = 9;
        return false;
    }
    if (!launch_user_elf(image, image_size, result.entry, result.stage,
                         result.failure)) {
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_user_musl_dir_pass;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    musl_blkdir_probe = result;
    return true;
}

const MuslBlkdirProbeResult& musl_blkdir_probe_result() {
    return musl_blkdir_probe;
}

bool exec_replace(const char* path, cpu::InterruptFrame* frame,
                  uint32_t& failure, const char* const* argv, uint32_t argc,
                  const char* const* envp, uint32_t envc) {
    failure = 0;
    if (path == nullptr || frame == nullptr) {
        failure = 1;
        return false;
    }
    const uint64_t old_root = syscall::session().user_root;
    if (old_root == 0 || smp::current_cpu_index() != 0) {
        failure = 2;
        return false;
    }

    const int64_t fd = fs::open_path(path, fs::o_rdonly, 0);
    if (fd < 0) {
        failure = 3;
        return false;
    }
    // BSS image buffer: sector FS allows ≤4 MiB files; avoid full memset.
    static uint8_t image[cxcfs::max_file_bytes];
    const int64_t got =
        fs::read_fd(static_cast<uint32_t>(fd), image, cxcfs::max_file_bytes);
    (void)fs::close_fd(static_cast<uint32_t>(fd));
    if (got <= 0) {
        failure = 4;
        return false;
    }
    const size_t image_size = static_cast<size_t>(got);

    const uint64_t kernel_root = vmm::kernel_address_space();
    if (!vmm::activate_address_space(kernel_root)) {
        failure = 5;
        return false;
    }

    const uint64_t new_root = vmm::create_user_address_space();
    if (new_root == 0) {
        (void)vmm::activate_address_space(old_root);
        failure = 6;
        return false;
    }

    elf::LoadResult loaded{};
    if (!elf::load_static_et_exec(new_root, image, image_size, loaded)) {
        (void)vmm::destroy_user_address_space(new_root);
        (void)vmm::activate_address_space(old_root);
        failure = 10 + loaded.failure;
        return false;
    }

    uint64_t stack_physical = 0;
    uint64_t top_page_va = 0;
    if (!map_user_stack(new_root, stack_physical, top_page_va)) {
        (void)vmm::destroy_user_address_space(new_root);
        (void)vmm::activate_address_space(old_root);
        failure = 7;
        return false;
    }

    (void)top_page_va;
    auto* const stack_base = reinterpret_cast<uint8_t*>(
        vmm::direct_map_base + stack_physical);
    const uint64_t user_rsp = seed_linux_user_stack(
        stack_base, user_stack_base, user_stack_pages, loaded.entry,
        loaded.at_phdr, loaded.at_phent, loaded.at_phnum, argv, argc, envp,
        envc);

    if (!vmm::destroy_user_address_space(old_root)) {
        (void)vmm::destroy_user_address_space(new_root);
        failure = 8;
        return false;
    }
    if (!vmm::activate_address_space(new_root)) {
        failure = 9;
        return false;
    }

    syscall::set_user_root(new_root);
    syscall::reset_heap_after_exec();

    frame->rip = loaded.entry;
    frame->rsp = user_rsp;
    frame->cs = cpu::user_cs;
    frame->ss = cpu::user_ss;
    frame->rflags = (frame->rflags | 0x200ull) & ~uint64_t{0x100};  // IF, clear TF
    frame->rax = 0;
    frame->rdi = 0;
    frame->rsi = 0;
    frame->rdx = 0;
    return true;
}

bool run_disk_exec_probe(const uint8_t* stub_image, size_t stub_size,
                         const uint8_t* payload_image, size_t payload_size,
                         DiskExecProbeResult& result) {
    result = DiskExecProbeResult{};
    result.stage = 1;
    if (stub_image == nullptr || stub_size == 0 || payload_image == nullptr ||
        payload_size == 0 || !cxcfs::is_mounted()) {
        result.failure = 1;
        return false;
    }
    if (payload_size > cxcfs::max_file_bytes) {
        result.failure = 2;
        return false;
    }

    const int64_t wfd = fs::open_path(
        "disk/run.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
    if (wfd < 0) {
        result.failure = 3;
        return false;
    }
    if (fs::write_fd(static_cast<uint32_t>(wfd), payload_image, payload_size) !=
        static_cast<int64_t>(payload_size)) {
        (void)fs::close_fd(static_cast<uint32_t>(wfd));
        result.failure = 4;
        return false;
    }
    if (fs::close_fd(static_cast<uint32_t>(wfd)) != 0) {
        result.failure = 5;
        return false;
    }
    result.wrote_elf = 1;
    result.stage = 2;

    cxcfs::MountResult remounted{};
    if (!cxcfs::remount(remounted)) {
        result.failure = 6;
        return false;
    }
    result.remount_ok = 1;
    result.stage = 3;

    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(stub_image, stub_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 20 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_disk_exec_pass;
    result.argv_pass = syscall::session().saw_disk_argv_pass;
    result.env_pass = syscall::session().saw_disk_env_pass;
    result.elf_bytes = static_cast<uint32_t>(payload_size);
    result.stage = 4;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        result.argv_pass == 0 || result.env_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    disk_exec_probe = result;
    return true;
}

const DiskExecProbeResult& disk_exec_probe_result() {
    return disk_exec_probe;
}

bool run_fork_wait_probe(const uint8_t* image, size_t image_size,
                         ForkWaitProbeResult& result) {
    result = ForkWaitProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_fork_wait_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    fork_wait_probe = result;
    return true;
}

const ForkWaitProbeResult& fork_wait_probe_result() {
    return fork_wait_probe;
}

bool run_pipe_probe(const uint8_t* image, size_t image_size,
                    PipeProbeResult& result) {
    result = PipeProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_pipe_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    pipe_probe = result;
    return true;
}

const PipeProbeResult& pipe_probe_result() {
    return pipe_probe;
}

bool run_chdir_probe(const uint8_t* image, size_t image_size,
                     ChdirProbeResult& result) {
    result = ChdirProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_chdir_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    chdir_probe = result;
    return true;
}

const ChdirProbeResult& chdir_probe_result() {
    return chdir_probe;
}

bool run_nest_probe(const uint8_t* image, size_t image_size,
                    NestProbeResult& result) {
    result = NestProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_nest_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    nest_probe = result;
    return true;
}

const NestProbeResult& nest_probe_result() {
    return nest_probe;
}

bool run_cap_probe(const uint8_t* image, size_t image_size,
                   CapProbeResult& result) {
    result = CapProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_cap_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    cap_probe = result;
    return true;
}

const CapProbeResult& cap_probe_result() {
    return cap_probe;
}

bool run_load128_probe(const uint8_t* stub_image, size_t stub_size,
                       const uint8_t* payload_image, size_t payload_size,
                       Load128ProbeResult& result) {
    constexpr size_t target_bytes = 100 * 1024;
    result = Load128ProbeResult{};
    result.stage = 1;
    if (stub_image == nullptr || stub_size == 0 || payload_image == nullptr ||
        payload_size == 0 || !cxcfs::is_mounted()) {
        result.failure = 1;
        return false;
    }
    if (payload_size > target_bytes || target_bytes > cxcfs::max_file_bytes) {
        result.failure = 2;
        return false;
    }

    const int64_t wfd = fs::open_path(
        "disk/load128.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
    if (wfd < 0) {
        result.failure = 3;
        return false;
    }
    if (fs::write_fd(static_cast<uint32_t>(wfd), payload_image, payload_size) !=
        static_cast<int64_t>(payload_size)) {
        (void)fs::close_fd(static_cast<uint32_t>(wfd));
        result.failure = 4;
        return false;
    }
    static uint8_t pad[4096];
    memset(pad, 0xA5, sizeof(pad));
    size_t remaining = target_bytes - payload_size;
    while (remaining > 0) {
        const size_t chunk =
            remaining < sizeof(pad) ? remaining : sizeof(pad);
        if (fs::write_fd(static_cast<uint32_t>(wfd), pad, chunk) !=
            static_cast<int64_t>(chunk)) {
            (void)fs::close_fd(static_cast<uint32_t>(wfd));
            result.failure = 5;
            return false;
        }
        remaining -= chunk;
    }
    if (fs::close_fd(static_cast<uint32_t>(wfd)) != 0) {
        result.failure = 6;
        return false;
    }
    result.wrote_elf = 1;
    result.elf_bytes = static_cast<uint32_t>(target_bytes);
    result.stage = 2;

    cxcfs::MountResult remounted{};
    if (!cxcfs::remount(remounted)) {
        result.failure = 7;
        return false;
    }
    result.remount_ok = 1;
    result.stage = 3;

    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(stub_image, stub_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 20 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_load128_pass;
    result.stage = 4;

    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 8;
        return false;
    }
    syscall::clear_session();
    load128_probe = result;
    return true;
}

const Load128ProbeResult& load128_probe_result() {
    return load128_probe;
}

bool run_heap_probe(const uint8_t* image, size_t image_size,
                    HeapProbeResult& result) {
    result = HeapProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_heap_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    heap_probe = result;
    return true;
}

const HeapProbeResult& heap_probe_result() {
    return heap_probe;
}

bool run_time_probe(const uint8_t* image, size_t image_size,
                    TimeProbeResult& result) {
    result = TimeProbeResult{};
    result.stage = 1;
    if (image == nullptr || image_size == 0) {
        result.failure = 1;
        return false;
    }
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(image, image_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 10 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_time_pass;
    result.stage = 2;
    if (result.exit_code != 0 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 2;
        return false;
    }
    syscall::clear_session();
    time_probe = result;
    return true;
}

const TimeProbeResult& time_probe_result() {
    return time_probe;
}

bool run_onbox_cc_probe(const uint8_t* stub_image, size_t stub_size,
                        const uint8_t* cc_image, size_t cc_size,
                        OnboxCcProbeResult& result_out) {
    // Keep the working result in .bss — M260 fat plant + musl-static as
    // push UEFI stack hard enough to clobber a caller stack-local result.
    OnboxCcProbeResult& result = onbox_cc_probe;
    // M204: ctype isupper/isblank/isgraph/isprint + sete/jbe; layout kept.
    static const char lib_c_text[] =
        "CXC_A2\n"
        "struct Pt {\n"
        "char t;\n"
        "int x;\n"
        "int y;\n"
        "};\n"
        "struct Pair {\n"
        "char a;\n"
        "char b;\n"
        "};\n"
        "typedef unsigned long size_t;\n"
        "import fn a_sum();\n"
        "import fn b_val();\n"
        "fn score(){\n"
        "Pt p;\n"
        "Pt* sp;\n"
        "Pair q;\n"
        "void* vp;\n"
        "void** vpp;\n"
        "long n;\n"
        "unsigned u;\n"
        "size_t z;\n"
        "char* cp;\n"
        "char** pp;\n"
        "p.t=1;\n"
        "p.x=6;\n"
        "p.y=7;\n"
        "sp=&p;\n"
        "vp=sp;\n"
        "vpp=&vp;\n"
        "n=sp->x*sp->y;\n"
        "u=42;\n"
        "z=sizeof(char);\n"
        "if(z!=1){return 1;}\n"
        "z=sizeof(size_t);\n"
        "if(z!=8){return 2;}\n"
        "z=sizeof(void*);\n"
        "if(z!=8){return 3;}\n"
        "z=sizeof(char**);\n"
        "if(z!=8){return 5;}\n"
        "z=sizeof(void**);\n"
        "if(z!=8){return 7;}\n"
        "z=sizeof(Pt);\n"
        "if(z!=24){return 8;}\n"
        "z=sizeof(Pair);\n"
        "if(z!=2){return 9;}\n"
        "q.a=1;\n"
        "q.b=2;\n"
        "cp=\"ok\";\n"
        "pp=&cp;\n"
        "if(n!=42){return 4;}\n"
        "if(u!=42){return 6;}\n"
        "if(a_sum()!=42){return 10;}\n"
        "if(b_val()!=42){return 11;}\n"
        "return n;\n"
        "}\n";
    static const char a_c_text[] =
        "CXC_A2\n"
        "#include \"hdr.h\"\n"
        "fn a_sum(){\n"
        "int a[4];\n"
        "int i;\n"
        "int s;\n"
        "s=0;\n"
        "a[0]=10;\n"
        "a[1]=10;\n"
        "a[2]=10;\n"
        "a[3]=12;\n"
        "for(i=0;i<4;i=i+1){s=s+a[i];}\n"
        "return s;\n"
        "}\n";
    static const char b_c_text[] =
        "CXC_A2\n"
        "fn b_val(){\n"
        "int n;\n"
        "n=0;\n"
        "while(n<42){n=n+1;}\n"
        "return n;\n"
        "}\n";
    /* Empty include body — lexer has no // or block-comment subset. */
    static const char hdr_h_text[] = "\n";
    static const char main_s_text[] =
        ".text\n"
        ".globl main\n"
        "main:\n"
        "push %rbp\n"
        "mov %rsp, %rbp\n"
        "sub $32, %rsp\n"
        "mov $6, %eax\n"
        "imul $7, %eax\n"
        "cmp $42, %eax\n"
        "jne .Lim\n"
        "mov $84, %eax\n"
        "cdq\n"
        "mov $2, %ecx\n"
        "idiv %ecx\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "xor %edx, %edx\n"
        "mov $84, %eax\n"
        "mov $2, %ecx\n"
        "div %ecx\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "mov $21, %eax\n"
        "shl $1, %eax\n"
        "mov $42, %ecx\n"
        "cmp %ecx, %eax\n"
        "jne .Ldv\n"
        "mov $42, %eax\n"
        "mov $255, %ecx\n"
        "and %ecx, %eax\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "mov $298, %eax\n"
        "movzbl %al, %eax\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "mov $-42, %eax\n"
        "neg %eax\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "mov $214, %eax\n"
        "movsbl %al, %eax\n"
        "cmp $-42, %eax\n"
        "jne .Ldv\n"
        "neg %eax\n"
        "cmp $42, %eax\n"
        "jne .Ldv\n"
        "endbr64\n"
        "mov $42, %edi\n"
        "cmp $42, %edi\n"
        "sete %al\n"
        "movzbl %al, %eax\n"
        "cmp $1, %eax\n"
        "jne .Ldv\n"
        "mov $50, %edi\n"
        "sub $8, %edi\n"
        "cmp $42, %edi\n"
        "jne .Ldv\n"
        "mov $40, %edi\n"
        "cmp $42, %edi\n"
        "setne %dl\n"
        "movzbl %dl, %eax\n"
        "cmp $1, %eax\n"
        "jne .Ldv\n"
        "mov $40, %edi\n"
        "cmp $42, %edi\n"
        "jbe .Lokj\n"
        "jmp .Ldv\n"
        ".Lokj:\n"
        "mov $50, %edi\n"
        "cmp $42, %edi\n"
        "jae .Lokb\n"
        "jmp .Ldv\n"
        ".Lokb:\n"
        "mov $40, %edi\n"
        "cmp $42, %edi\n"
        "jb .Lokc\n"
        "jmp .Ldv\n"
        ".Lokc:\n"
        "xor %edx, %edx\n"
        "mov q(%rip), %rax\n"
        "cmp $42, %eax\n"
        "jne .Lmv\n"
        "call score\n"
        "cmp $42, %eax\n"
        "jne .Lsc\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea s4(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcpy\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea s2(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcat\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $2, %edx\n"
        "call strncmp\n"
        "cmp $0, %eax\n"
        "jne .Lstr\n"
        "lea buf2(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea sa(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call stpcpy\n"
        "lea buf2(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea sa(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcmp\n"
        "cmp $0, %eax\n"
        "jne .Lstr\n"
        "mov $53, %edi\n"
        "call isdigit\n"
        "test %eax, %eax\n"
        "je .Lstr\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "call atoi\n"
        "cmp $42, %eax\n"
        "jne .Lstr\n"
        "lea -16(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $3, %edx\n"
        "call memcpy\n"
        "lea -16(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "mov $52, %esi\n"
        "mov $2, %edx\n"
        "call memchr\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea -16(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $2, %edx\n"
        "call memcmp\n"
        "cmp $0, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $50, %esi\n"
        "call strchr\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "xor %esi, %esi\n"
        "call strchrnul\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "mov $65, %edi\n"
        "call tolower\n"
        "cmp $97, %eax\n"
        "jne .Lmc\n"
        "mov $97, %edi\n"
        "call toupper\n"
        "cmp $65, %eax\n"
        "jne .Lmc\n"
        "mov $32, %edi\n"
        "call isspace\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $65, %edi\n"
        "call isalpha\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $50, %edi\n"
        "call isalnum\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $97, %edi\n"
        "call islower\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $65, %edi\n"
        "call isupper\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $32, %edi\n"
        "call isblank\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $65, %edi\n"
        "call isgraph\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $32, %edi\n"
        "call isprint\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $65, %edi\n"
        "call isxdigit\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $7, %edi\n"
        "call iscntrl\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $33, %edi\n"
        "call ispunct\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $-42, %edi\n"
        "call abs\n"
        "cmp $42, %eax\n"
        "jne .Lmc\n"
        "mov $65, %edi\n"
        "call isascii\n"
        "test %eax, %eax\n"
        "je .Lmc\n"
        "mov $42, %edi\n"
        "call llabs\n"
        "cmp $42, %eax\n"
        "jne .Lmc\n"
        "lea -8(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "xor %esi, %esi\n"
        "mov $4, %edx\n"
        "call memset\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $4, %esi\n"
        "call bzero\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $8, %esi\n"
        "call strnlen\n"
        "cmp $2, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "movzbl (%rdi), %eax\n"
        "cmp $52, %eax\n"
        "jne .Lmc\n"
        "add $1, %rdi\n"
        "movzbl (%rdi), %eax\n"
        "cmp $50, %eax\n"
        "jne .Lmc\n"
        "mov $1, %edx\n"
        "test %rdx, %rdx\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $52, %eax\n"
        "cmp %al, (%rdi)\n"
        "jne .Lmc\n"
        "mov $0x141, %edi\n"
        "call toascii\n"
        "cmp $65, %eax\n"
        "jne .Lmc\n"
        "mov $42, %edi\n"
        "call imaxabs\n"
        "cmp $42, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $2, %edx\n"
        "call bcmp\n"
        "test %eax, %eax\n"
        "jne .Lmc\n"
        "mov $8, %edi\n"
        "call ffs\n"
        "cmp $4, %eax\n"
        "jne .Lmc\n"
        "mov $0x1234, %edi\n"
        "call htons\n"
        "cmp $0x3412, %eax\n"
        "jne .Lmc\n"
        "mov $0x12345678, %edi\n"
        "call htonl\n"
        "cmp $0x78563412, %eax\n"
        "jne .Lmc\n"
        "mov $0x3412, %edi\n"
        "call ntohs\n"
        "cmp $0x1234, %eax\n"
        "jne .Lmc\n"
        "mov $0x78563412, %edi\n"
        "call ntohl\n"
        "cmp $0x12345678, %eax\n"
        "jne .Lmc\n"
        "mov $40, %edi\n"
        "inc %rdi\n"
        "inc %rdi\n"
        "cmp $42, %edi\n"
        "jne .Lmc\n"
        "mov $3, %edx\n"
        "sub $1, %rdx\n"
        "cmp $2, %edx\n"
        "jne .Lmc\n"
        "mov $40, %edi\n"
        "cmp $42, %edi\n"
        "setb %cl\n"
        "movzbl %cl, %eax\n"
        "cmp $1, %eax\n"
        "jne .Lmc\n"
        "mov $50, %edi\n"
        "cmp $42, %edi\n"
        "seta %cl\n"
        "movzbl %cl, %eax\n"
        "cmp $1, %eax\n"
        "jne .Lmc\n"
        "mov $0x1234, %eax\n"
        "rolw $8, %ax\n"
        "cmp $0x3412, %eax\n"
        "jne .Lmc\n"
        "mov $0x12345678, %eax\n"
        "bswap %eax\n"
        "cmp $0x78563412, %eax\n"
        "jne .Lmc\n"
        "mov $0xab, %eax\n"
        "movzwl %ax, %eax\n"
        "cmp $0xab, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea ssep(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcspn\n"
        "cmp $2, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea sdig(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strspn\n"
        "cmp $2, %eax\n"
        "jne .Lmc\n"
        "lea spair(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea ssep(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strpbrk\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $50, %esi\n"
        "call strrchr\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea -16(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $3, %edx\n"
        "call memmove\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "call strdup\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "xor %esi, %esi\n"
        "mov $10, %edx\n"
        "call strtol\n"
        "cmp $42, %eax\n"
        "jne .Lmc\n"
        "mov $42, %edi\n"
        "call labs\n"
        "leave\n"
        "and $255, %eax\n"
        "ret\n"
        ".Lim:\n"
        "mov $22, %eax\n"
        "jmp .Lout\n"
        ".Ldv:\n"
        "mov $25, %eax\n"
        "jmp .Lout\n"
        ".Lmv:\n"
        "mov $23, %eax\n"
        "jmp .Lout\n"
        ".Lsc:\n"
        "and $255, %eax\n"
        "jmp .Lout\n"
        ".Lstr:\n"
        "mov $24, %eax\n"
        "jmp .Lout\n"
        ".Lmc:\n"
        "mov $26, %eax\n"
        "jmp .Lout\n"
        ".Lout:\n"
        "mov %rbp, %rsp\n"
        "pop %rbp\n"
        "ret\n"
        ".data\n"
        "buf:\n"
        ".zero 16\n"
        "buf2:\n"
        ".zero 16\n"
        ".rodata\n"
        "sa:\n"
        ".asciz \"HD\"\n"
        "s4:\n"
        ".asciz \"4\"\n"
        "s2:\n"
        ".asciz \"2\"\n"
        "s42:\n"
        ".asciz \"42\"\n"
        "spair:\n"
        ".asciz \"4,2\"\n"
        "ssep:\n"
        ".asciz \",;\"\n"
        "sdig:\n"
        ".asciz \"0123456789\"\n"
        "q:\n"
        ".quad 42\n";
    /* CCDRV job.rsp: .s first (small forks), nested a.c/b.c, then lib.c. */
    static const char job_rsp_text[] =
        "disk/main.s\n"
        "disk/x1.s\n"
        "disk/x2.s\n"
        "disk/t.s\n"
        "disk/a.c\n"
        "disk/b.c\n"
        "disk/lib.c\n"
        "disk/atoi.s\n"
        "disk/scmp.s\n"
        "disk/labs.s\n"
        "disk/dig.s\n"
        "disk/scat.s\n"
        "disk/schr.s\n"
        "disk/snul.s\n"
        "disk/scpy.s\n"
        "disk/stpc.s\n"
        "disk/sncmp.s\n"
        "disk/mcmp.s\n"
        "disk/mchr.s\n"
        "disk/tlwr.s\n"
        "disk/tupr.s\n"
        "disk/ispc.s\n"
        "disk/ialp.s\n"
        "disk/ialn.s\n"
        "disk/ilwr.s\n"
        "disk/iupr.s\n"
        "disk/iblk.s\n"
        "disk/igrp.s\n"
        "disk/iprt.s\n"
        "disk/ixdg.s\n"
        "disk/ictl.s\n"
        "disk/ipct.s\n"
        "disk/iabs.s\n"
        "disk/bzro.s\n"
        "disk/snln.s\n"
        "disk/iasc.s\n"
        "disk/llab.s\n"
        "disk/tasc.s\n"
        "disk/imax.s\n"
        "disk/bcmp.s\n"
        "disk/iffs.s\n"
        "disk/htns.s\n"
        "disk/htnl.s\n"
        "disk/ntns.s\n"
        "disk/ntnl.s\n"
        "disk/mset.s\n"
        "disk/scsp.o\n"
        "disk/sspn.o\n"
        "disk/spbk.o\n"
        "disk/srch.o\n"
        "disk/mmov.o\n"
        "disk/sdup.o\n"
        "disk/stol.o\n"
        "disk/iscn.o\n"
        "disk/shgt.o\n"
        "disk/mrch.o\n"
        "disk/hstb.o\n"
        "disk/mcpy.o\n"
        "disk/slen.s\n"
        "disk/tcap_br.o\n";
    static const char src_text[] =
        "CXC_A2\n"
        "fn main(){\n"
        "return 0;\n"
        "}\n";
    static const char bad_src[] =
        "CXC_A2\n"
        "#lib \"crt.elf\"\n"
        "import fn ans();\n"
        "fn main(){\n"
        "return ans();\n"
        "}\n";
    result = OnboxCcProbeResult{};
    result.stage = 1;
    if (stub_image == nullptr || stub_size == 0 || cc_image == nullptr ||
        cc_size == 0 || !cxcfs::is_mounted()) {
        result.failure = 1;
        return false;
    }
    if (cc_size > cxcfs::max_file_bytes) {
        result.failure = 2;
        return false;
    }

    {
        const int64_t lfd = fs::open_path(
            "disk/rt.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (lfd < 0) {
            result.failure = 16;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(lfd), rtmath_elf,
                         rtmath_elf_size) !=
            static_cast<int64_t>(rtmath_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(lfd));
            result.failure = 17;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(lfd)) != 0) {
            result.failure = 18;
            return false;
        }
    }
    {
        const int64_t crtfd = fs::open_path(
            "disk/crt.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (crtfd < 0) {
            result.failure = 40;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(crtfd), rtcrt_elf,
                         rtcrt_elf_size) !=
            static_cast<int64_t>(rtcrt_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(crtfd));
            result.failure = 41;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(crtfd)) != 0) {
            result.failure = 42;
            return false;
        }
    }
    {
        const int64_t iofd = fs::open_path(
            "disk/io.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (iofd < 0) {
            result.failure = 43;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(iofd), rtio_elf, rtio_elf_size) !=
            static_cast<int64_t>(rtio_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(iofd));
            result.failure = 44;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(iofd)) != 0) {
            result.failure = 45;
            return false;
        }
    }

    {
        const int64_t mfd = fs::open_path(
            "disk/math.o", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (mfd < 0) {
            result.failure = 49;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(mfd), host_add_elf,
                         host_add_elf_size) !=
            static_cast<int64_t>(host_add_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(mfd));
            result.failure = 50;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(mfd)) != 0) {
            result.failure = 51;
            return false;
        }
    }
    {
        const int64_t hfd = fs::open_path(
            "disk/host.o", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (hfd < 0) {
            result.failure = 46;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(hfd), host_bump_elf,
                         host_bump_elf_size) !=
            static_cast<int64_t>(host_bump_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(hfd));
            result.failure = 47;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(hfd)) != 0) {
            result.failure = 48;
            return false;
        }
    }
    {
        const int64_t yfd = fs::open_path(
            "disk/mcpy.o", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (yfd < 0) {
            result.failure = 64;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(yfd), musl_memcpy_elf,
                         musl_memcpy_elf_size) !=
            static_cast<int64_t>(musl_memcpy_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(yfd));
            result.failure = 65;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(yfd)) != 0) {
            result.failure = 66;
            return false;
        }
    }
    {
        struct Plant {
            const char* path;
            const unsigned char* blob;
            unsigned size;
            uint32_t fail;
        };
        const Plant plants[] = {
            {"disk/scsp.o", musl_strcspn_elf, musl_strcspn_elf_size, 200},
            {"disk/sspn.o", musl_strspn_elf, musl_strspn_elf_size, 203},
            {"disk/spbk.o", musl_strpbrk_elf, musl_strpbrk_elf_size, 206},
            {"disk/srch.o", musl_strrchr_elf, musl_strrchr_elf_size, 209},
            {"disk/mmov.o", musl_memmove_elf, musl_memmove_elf_size, 212},
            {"disk/sdup.o", musl_strdup_elf, musl_strdup_elf_size, 215},
            {"disk/stol.o", musl_strtol_elf, musl_strtol_elf_size, 218},
            {"disk/iscn.o", musl_intscan_elf, musl_intscan_elf_size, 221},
            {"disk/shgt.o", musl_shgetc_elf, musl_shgetc_elf_size, 224},
            {"disk/mrch.o", musl_memrchr_elf, musl_memrchr_elf_size, 227},
            {"disk/hstb.o", hard_stubs_elf, hard_stubs_elf_size, 230},
        };
        for (const Plant& pl : plants) {
            const int64_t pfd = fs::open_path(
                pl.path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
            if (pfd < 0) {
                result.failure = pl.fail;
                return false;
            }
            if (fs::write_fd(static_cast<uint32_t>(pfd), pl.blob, pl.size) !=
                static_cast<int64_t>(pl.size)) {
                (void)fs::close_fd(static_cast<uint32_t>(pfd));
                result.failure = pl.fail + 1;
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(pfd)) != 0) {
                result.failure = pl.fail + 2;
                return false;
            }
        }
    }

    {
        const int64_t lfd = fs::open_path(
            "disk/lib.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (lfd < 0) {
            result.failure = 70;
            return false;
        }
        const size_t lib_len = sizeof(lib_c_text) - 1;
        if (fs::write_fd(static_cast<uint32_t>(lfd), lib_c_text, lib_len) !=
            static_cast<int64_t>(lib_len)) {
            (void)fs::close_fd(static_cast<uint32_t>(lfd));
            result.failure = 71;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(lfd)) != 0) {
            result.failure = 72;
            return false;
        }
    }
    {
        const int64_t afd = fs::open_path(
            "disk/main.s", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (afd < 0) {
            result.failure = 94;
            return false;
        }
        const size_t main_len = sizeof(main_s_text) - 1;
        if (fs::write_fd(static_cast<uint32_t>(afd), main_s_text, main_len) !=
            static_cast<int64_t>(main_len)) {
            (void)fs::close_fd(static_cast<uint32_t>(afd));
            result.failure = 95;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(afd)) != 0) {
            result.failure = 96;
            return false;
        }
    }
    {
        const int64_t rfd = fs::open_path(
            "disk/job.rsp", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (rfd < 0) {
            result.failure = 97;
            return false;
        }
        const size_t rsp_len = sizeof(job_rsp_text) - 1;
        if (fs::write_fd(static_cast<uint32_t>(rfd), job_rsp_text, rsp_len) !=
            static_cast<int64_t>(rsp_len)) {
            (void)fs::close_fd(static_cast<uint32_t>(rfd));
            result.failure = 98;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(rfd)) != 0) {
            result.failure = 99;
            return false;
        }
    }
    {
        struct SrcPlant2 {
            const char* path;
            const char* text;
            uint32_t fail;
        };
        const SrcPlant2 extras[] = {
            {"disk/a.c", a_c_text, 360},
            {"disk/b.c", b_c_text, 370},
            {"disk/hdr.h", hdr_h_text, 380},
        };
        for (const SrcPlant2& sp : extras) {
            const int64_t sfd = fs::open_path(
                sp.path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
            if (sfd < 0) {
                result.failure = sp.fail;
                return false;
            }
            size_t n = 0;
            while (sp.text[n] != 0) {
                ++n;
            }
            if (fs::write_fd(static_cast<uint32_t>(sfd), sp.text, n) !=
                static_cast<int64_t>(n)) {
                (void)fs::close_fd(static_cast<uint32_t>(sfd));
                result.failure = sp.fail + 1;
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(sfd)) != 0) {
                result.failure = sp.fail + 2;
                return false;
            }
        }
    }

    /* M295: shallow musl leaves as on-box .s (assembled by disk/as). */
    {
        for (int pi = 0; pi < PLANT_ASM_COUNT; ++pi) {
            const PlantAsmText& pa = plant_asm_texts[pi];
            const int64_t pfd = fs::open_path(
                pa.disk_path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
            if (pfd < 0) {
                result.failure = 500 + static_cast<uint32_t>(pi);
                return false;
            }
            size_t n = 0;
            while (pa.text[n] != 0) {
                ++n;
            }
            if (fs::write_fd(static_cast<uint32_t>(pfd), pa.text, n) !=
                static_cast<int64_t>(n)) {
                (void)fs::close_fd(static_cast<uint32_t>(pfd));
                result.failure = 600 + static_cast<uint32_t>(pi);
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(pfd)) != 0) {
                result.failure = 700 + static_cast<uint32_t>(pi);
                return false;
            }
        }
    }
    /* job.rsp: M300 batch3 — 13 prebuilt .o; 44 .s (4 base + 40 leaves). */
    result.planted = 13;
    result.inputs_s = 44;

    const int64_t sfd = fs::open_path(
        "disk/in.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
    if (sfd < 0) {
        result.failure = 3;
        return false;
    }
    const size_t src_len = sizeof(src_text) - 1;
    if (fs::write_fd(static_cast<uint32_t>(sfd), src_text, src_len) !=
        static_cast<int64_t>(src_len)) {
        (void)fs::close_fd(static_cast<uint32_t>(sfd));
        result.failure = 4;
        return false;
    }
    if (fs::close_fd(static_cast<uint32_t>(sfd)) != 0) {
        result.failure = 5;
        return false;
    }
    result.wrote_src = 1;
    result.stage = 2;

    const int64_t cfd = fs::open_path(
        "disk/cxc-cc", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
    if (cfd < 0) {
        result.failure = 6;
        return false;
    }
    if (fs::write_fd(static_cast<uint32_t>(cfd), cc_image, cc_size) !=
        static_cast<int64_t>(cc_size)) {
        (void)fs::close_fd(static_cast<uint32_t>(cfd));
        result.failure = 7;
        return false;
    }
    if (fs::close_fd(static_cast<uint32_t>(cfd)) != 0) {
        result.failure = 8;
        return false;
    }
    // Same ELF as disk/cc so nested `cc -c` hits audited path.
    {
        const int64_t cc2 = fs::open_path(
            "disk/cc", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
        if (cc2 < 0) {
            result.failure = 301;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(cc2), cc_image, cc_size) !=
            static_cast<int64_t>(cc_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(cc2));
            result.failure = 302;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(cc2)) != 0) {
            result.failure = 303;
            return false;
        }
    }
    result.wrote_cc = 1;
    result.stage = 3;

    // M266–M267: plant freestanding as/ld CLIs before driver spawn.
    {
        struct ToolPlant {
            const char* path;
            const unsigned char* blob;
            unsigned size;
            uint32_t fail;
        };
        const ToolPlant tools[] = {
            {"disk/as", as_cli_elf, as_cli_elf_size, 300},
            {"disk/ld", ld_cli_elf, ld_cli_elf_size, 310},
            {"disk/tcap_br.o", tcap_bridge_elf, tcap_bridge_elf_size, 320},
        };
        for (const ToolPlant& tp : tools) {
            const int64_t tfd = fs::open_path(
                tp.path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
            if (tfd < 0) {
                result.failure = tp.fail;
                return false;
            }
            if (fs::write_fd(static_cast<uint32_t>(tfd), tp.blob, tp.size) !=
                static_cast<int64_t>(tp.size)) {
                (void)fs::close_fd(static_cast<uint32_t>(tfd));
                result.failure = tp.fail + 1;
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(tfd)) != 0) {
                result.failure = tp.fail + 2;
                return false;
            }
        }
        static const char x1_s[] =
            ".text\n.globl tdrv_x1\ntdrv_x1:\nmov $1, %eax\nret\n";
        static const char x2_s[] =
            ".text\n.globl tdrv_x2\ntdrv_x2:\nmov $2, %eax\nret\n";
        static const char t_s[] =
            ".text\n"
            ".globl tcap_add\n"
            "tcap_add:\n"
            "mov $42, %eax\n"
            "ret\n";
        struct SrcPlant {
            const char* path;
            const char* text;
            uint32_t fail;
        };
        const SrcPlant srcs[] = {
            {"disk/x1.s", x1_s, 330},
            {"disk/x2.s", x2_s, 340},
            {"disk/t.s", t_s, 350},
        };
        for (const SrcPlant& sp : srcs) {
            const int64_t sfd = fs::open_path(
                sp.path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
            if (sfd < 0) {
                result.failure = sp.fail;
                return false;
            }
            size_t n = 0;
            while (sp.text[n] != 0) {
                ++n;
            }
            if (fs::write_fd(static_cast<uint32_t>(sfd), sp.text, n) !=
                static_cast<int64_t>(n)) {
                (void)fs::close_fd(static_cast<uint32_t>(sfd));
                result.failure = sp.fail + 1;
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(sfd)) != 0) {
                result.failure = sp.fail + 2;
                return false;
            }
        }
    }

    cxcfs::MountResult remounted{};
    if (!cxcfs::remount(remounted)) {
        result.failure = 9;
        return false;
    }
    result.remount_ok = 1;
    result.stage = 4;

    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(stub_image, stub_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 20 + failure;
        return false;
    }
    result.loaded = 1;
    result.entered = 1;
    result.exited = 1;
    result.exit_code = syscall::session().exit_code;
    result.wrote_pass = syscall::session().saw_onbox_cc_pass;
    // Kernel-audited spawn counts from this HARD session only (before clear).
    result.spawn_as = syscall::session().exec_disk_as;
    result.spawn_ld = syscall::session().exec_disk_ld;
    result.spawn_cc = syscall::session().exec_disk_cc;
    result.inputs_c = 3; /* job.rsp: a.c + b.c + lib.c */
    /* inputs_s / planted set at plant time (M295). */
    result.stage = 5;

    if (result.exit_code != 42 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 10;
        return false;
    }
    /* M295/M300: planted only decreases; hard cap ≤22 (M298 lock). */
    if (result.planted > 22) {
        syscall::clear_session();
        result.failure = 295;
        return false;
    }
    /* M293 spawn_cc dual window + M295 spawn_as dual window.
     * spawn_as: legacy 4 OR relational inputs_s (4 + rewritten leaves). */
    if (result.spawn_ld != 1) {
        syscall::clear_session();
        result.failure = 291;
        return false;
    }
    if (!(result.spawn_as == 4 || result.spawn_as == result.inputs_s)) {
        syscall::clear_session();
        result.failure = 291;
        return false;
    }
    if (!(result.spawn_cc == 3 ||
          result.spawn_cc == 1u + result.inputs_c)) {
        syscall::clear_session();
        result.failure = 291;
        return false;
    }
    syscall::clear_session();

    // HARD products: nested cc .o + as .o + ld ET_EXEC.
    {
        auto elf_type_ok = [](const char* path, unsigned char want_type) -> bool {
            const int64_t fd = fs::open_path(path, fs::o_rdonly, 0);
            if (fd < 0) {
                return false;
            }
            unsigned char hdr[20]{};
            const int64_t got =
                fs::read_fd(static_cast<uint32_t>(fd), hdr, sizeof(hdr));
            (void)fs::close_fd(static_cast<uint32_t>(fd));
            return got >= 20 && hdr[0] == 0x7f && hdr[1] == 'E' &&
                   hdr[2] == 'L' && hdr[3] == 'F' && hdr[16] == want_type &&
                   hdr[17] == 0;
        };
        if (!elf_type_ok("disk/lib.o", 1) || !elf_type_ok("disk/a.o", 1) ||
            !elf_type_ok("disk/b.o", 1) || !elf_type_ok("disk/main.o", 1) ||
            !elf_type_ok("disk/x1.o", 1) || !elf_type_ok("disk/x2.o", 1) ||
            !elf_type_ok("disk/t.o", 1)) {
            result.failure = 292;
            return false;
        }
        if (!elf_type_ok("disk/out.elf", 2)) {
            result.failure = 293;
            return false;
        }
    }

    // Edge: rewrite job.rsp → bad.c only (UND via nested cc -c + ld).
    result.stage = 6;
    if (fs::unlink_path("disk/job.rsp") != 0) {
        result.failure = 100;
        return false;
    }
    {
        const int64_t bfd = fs::open_path(
            "disk/bad.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (bfd < 0) {
            result.failure = 11;
            return false;
        }
        const size_t bad_len = sizeof(bad_src) - 1;
        if (fs::write_fd(static_cast<uint32_t>(bfd), bad_src, bad_len) !=
            static_cast<int64_t>(bad_len)) {
            (void)fs::close_fd(static_cast<uint32_t>(bfd));
            result.failure = 12;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(bfd)) != 0) {
            result.failure = 13;
            return false;
        }
    }
    {
        static const char bad_job[] = "disk/bad.c\n";
        const int64_t jfd = fs::open_path(
            "disk/job.rsp", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (jfd < 0) {
            result.failure = 101;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(jfd), bad_job,
                         sizeof(bad_job) - 1) !=
            static_cast<int64_t>(sizeof(bad_job) - 1)) {
            (void)fs::close_fd(static_cast<uint32_t>(jfd));
            result.failure = 102;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(jfd)) != 0) {
            result.failure = 103;
            return false;
        }
    }
    cxcfs::MountResult remount_edge{};
    if (!cxcfs::remount(remount_edge)) {
        result.failure = 14;
        return false;
    }
    entry = 0;
    stage = 0;
    failure = 0;
    if (!launch_user_elf(stub_image, stub_size, entry, stage, failure)) {
        result.stage = stage;
        result.failure = 30 + failure;
        return false;
    }
    {
        const uint64_t edge_exit = syscall::session().exit_code;
        const uint32_t edge_pass = syscall::session().saw_onbox_cc_pass;
        const uint32_t edge_dmsg = syscall::session().saw_onbox_dmsg;
        syscall::clear_session();
        if (edge_exit != 4 || edge_pass != 0 || edge_dmsg == 0) {
            result.failure = 15;
            result.exit_code = edge_exit;
            return false;
        }
    }
    result.edge_ok = 1;
    result.stage = 7;

    // M260: plant >1 MiB static ELF and execve (long argv via fat stub).
    {
        constexpr size_t fat_target = 1024u * 1024u + 512u;
        const int64_t ffd = fs::open_path(
            "disk/fat.elf", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
        if (ffd < 0) {
            result.failure = 240;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(ffd), fat42_elf,
                         fat42_elf_size) !=
            static_cast<int64_t>(fat42_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(ffd));
            result.failure = 241;
            return false;
        }
        static uint8_t pad[4096];
        for (size_t i = 0; i < sizeof(pad); ++i) {
            pad[i] = 0xA5;
        }
        size_t remaining = fat_target - fat42_elf_size;
        while (remaining > 0) {
            const size_t chunk =
                remaining < sizeof(pad) ? remaining : sizeof(pad);
            if (fs::write_fd(static_cast<uint32_t>(ffd), pad, chunk) !=
                static_cast<int64_t>(chunk)) {
                (void)fs::close_fd(static_cast<uint32_t>(ffd));
                result.failure = 242;
                return false;
            }
            remaining -= chunk;
        }
        if (fs::close_fd(static_cast<uint32_t>(ffd)) != 0) {
            result.failure = 243;
            return false;
        }
        result.fat_bytes = static_cast<uint32_t>(fat_target);
        cxcfs::MountResult fat_remount{};
        if (!cxcfs::remount(fat_remount)) {
            result.failure = 244;
            return false;
        }
        {
            const int64_t rfd = fs::open_path("disk/fat.elf", fs::o_rdonly, 0);
            if (rfd < 0) {
                result.failure = 246;
                return false;
            }
            fs::StatBuf st{};
            if (fs::fstat_fd(static_cast<uint32_t>(rfd), st) != 0 ||
                st.st_size != static_cast<int64_t>(fat_target)) {
                (void)fs::close_fd(static_cast<uint32_t>(rfd));
                result.failure = 247;
                result.exit_code = static_cast<uint64_t>(st.st_size);
                return false;
            }
            (void)fs::close_fd(static_cast<uint32_t>(rfd));
        }
        uint64_t entry = 0;
        uint32_t stage = 0;
        uint32_t failure = 0;
        if (!launch_user_elf(fat_stub_elf, fat_stub_elf_size, entry, stage,
                             failure)) {
            result.stage = stage;
            result.failure = 250 + failure;
            return false;
        }
        const uint64_t fat_exit = syscall::session().exit_code;
        syscall::clear_session();
        if (fat_exit != 42) {
            result.failure = 245;
            result.exit_code = fat_exit;
            return false;
        }
        result.fat_ok = 1;
    }
    result.stage = 8;

    // M261/M266: HARD already spawned disk/as → disk/t.o; keep mini-as as
    // capacity plant (disk/mini_as) without overwriting the driver CLI.
    {
        const int64_t mfd = fs::open_path(
            "disk/mini_as", fs::o_wronly | fs::o_creat | fs::o_trunc, 0755);
        if (mfd < 0) {
            result.failure = 260;
            return false;
        }
        if (fs::write_fd(static_cast<uint32_t>(mfd), mini_as_elf,
                         mini_as_elf_size) !=
            static_cast<int64_t>(mini_as_elf_size)) {
            (void)fs::close_fd(static_cast<uint32_t>(mfd));
            result.failure = 261;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(mfd)) != 0) {
            result.failure = 262;
            return false;
        }
        cxcfs::MountResult as_remount{};
        if (!cxcfs::remount(as_remount)) {
            result.failure = 266;
            return false;
        }
        // Re-run as_stub against driver disk/as (CLI) → refresh disk/t.o.
        uint64_t entry = 0;
        uint32_t stage = 0;
        uint32_t failure = 0;
        if (!launch_user_elf(as_stub_elf, as_stub_elf_size, entry, stage,
                             failure)) {
            result.stage = stage;
            result.failure = 270 + failure;
            return false;
        }
        const uint64_t as_exit = syscall::session().exit_code;
        syscall::clear_session();
        if (as_exit != 0) {
            result.failure = 267;
            result.exit_code = as_exit;
            return false;
        }
        const int64_t tofd = fs::open_path("disk/t.o", fs::o_rdonly, 0);
        if (tofd < 0) {
            result.failure = 268;
            return false;
        }
        unsigned char hdr[20]{};
        const int64_t got =
            fs::read_fd(static_cast<uint32_t>(tofd), hdr, sizeof(hdr));
        (void)fs::close_fd(static_cast<uint32_t>(tofd));
        if (got < 20 || hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' ||
            hdr[3] != 'F' || hdr[16] != 1 || hdr[17] != 0) {
            result.failure = 269;
            return false;
        }
        result.as_ok = 1;
    }
    result.stage = 9;

    // M262: bridge plant — verify on-disk ET_REL bytes (not open-only / BSS write).
    {
        if (tcap_bridge_elf_size < 20 || tcap_bridge_elf[0] != 0x7f ||
            tcap_bridge_elf[1] != 'E' || tcap_bridge_elf[2] != 'L' ||
            tcap_bridge_elf[3] != 'F' || tcap_bridge_elf[16] != 1) {
            result.failure = 283;
            return false;
        }
        const int64_t bfd = fs::open_path("disk/tcap_br.o", fs::o_rdonly, 0);
        if (bfd < 0) {
            result.failure = 280;
            return false;
        }
        unsigned char hdr[20]{};
        const int64_t got =
            fs::read_fd(static_cast<uint32_t>(bfd), hdr, sizeof(hdr));
        (void)fs::close_fd(static_cast<uint32_t>(bfd));
        if (got < 20 || hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' ||
            hdr[3] != 'F' || hdr[16] != 1 || hdr[17] != 0) {
            result.failure = 281;
            return false;
        }
        result.bridge_ok = 1;
    }

    // Derive capacity / driver flags from evidence only — no volatile BSS paint.
    result.tcap = (result.fat_ok == 1 && result.as_ok == 1 &&
                   result.bridge_ok == 1)
                      ? 1u
                      : 0u;
    result.tdrv = ((result.spawn_as == 4 || result.spawn_as == result.inputs_s) &&
                   result.spawn_ld == 1)
                      ? 1u
                      : 0u;
    result.ccdrv = (result.wrote_pass == 1 && result.edge_ok == 1 &&
                    result.tcap == 1 && result.tdrv == 1 &&
                    result.spawn_cc >= 1)
                       ? 1u
                       : 0u;
    result.stage = 10;
    if (result.ccdrv != 1) {
        result.failure = 290;
        result_out = result;
        return false;
    }
    result_out = result;
    return true;
}

const OnboxCcProbeResult& onbox_cc_probe_result() {
    return onbox_cc_probe;
}

bool ide_save_path(const char* path, const char* source, size_t source_len,
                   uint32_t& failure) {
    failure = 0;
    if (path == nullptr || path[0] == 0 || source == nullptr ||
        source_len == 0) {
        failure = 1;
        return false;
    }
    (void)fs::unlink_path(path);
    const int64_t fd =
        fs::open_path(path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
    if (fd < 0) {
        failure = 2;
        return false;
    }
    if (fs::write_fd(static_cast<uint32_t>(fd), source, source_len) !=
        static_cast<int64_t>(source_len)) {
        (void)fs::close_fd(static_cast<uint32_t>(fd));
        failure = 3;
        return false;
    }
    if (fs::close_fd(static_cast<uint32_t>(fd)) != 0) {
        failure = 4;
        return false;
    }
    return true;
}

bool ide_save_source(const char* source, size_t source_len, uint32_t& failure) {
    return ide_save_path("disk/ide_main.c", source, source_len, failure);
}

uint32_t ide_stat_size(const char* path) {
    fs::StatBuf st{};
    if (path == nullptr || fs::stat_path(path, st) != 0 || st.st_size < 0) {
        return 0;
    }
    return static_cast<uint32_t>(st.st_size);
}

bool ide_build_and_run(const uint8_t* cc_image, size_t cc_size,
                       IdeRebuildResult& result, const char* src_path) {
    result.built = 0;
    result.ran = 0;
    result.exit_code = 0;
    result.out_bytes = 0;
    const char* src =
        (src_path != nullptr && src_path[0] != 0) ? src_path : "disk/ide_main.c";
    result.src_bytes = ide_stat_size(src);
    if (cc_image == nullptr || cc_size == 0) {
        result.failure = 10;
        return false;
    }
    result.stage = 10;
    // argv[3] is mutable path slot for active buffer builds.
    static char src_arg[fs::max_path];
    size_t i = 0;
    while (src[i] != 0 && i + 1 < sizeof(src_arg)) {
        src_arg[i] = src[i];
        ++i;
    }
    src_arg[i] = 0;
    const char* argv[] = {"disk/cc", "-o", "disk/ide_out.elf", src_arg,
                          nullptr};
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(cc_image, cc_size, entry, stage, failure, argv, 4)) {
        result.stage = 10 + stage;
        result.failure = 20 + failure;
        result.out_bytes = ide_stat_size("disk/ide_out.elf");
        return false;
    }
    result.built = 1;
    result.out_bytes = ide_stat_size("disk/ide_out.elf");
    result.ran = 1;
    result.exit_code = syscall::session().exit_code;
    result.stage = 11;
    syscall::clear_session();
    if (result.exit_code != 42) {
        result.failure = 30;
        return false;
    }
    return true;
}

bool ide_run_out(IdeRebuildResult& result) {
    result.ran = 0;
    result.exit_code = 0;
    result.out_bytes = ide_stat_size("disk/ide_out.elf");
    result.stage = 20;
    const int64_t ofd = fs::open_path("disk/ide_out.elf", fs::o_rdonly, 0);
    if (ofd < 0) {
        result.failure = 40;
        return false;
    }
    static uint8_t out_image[512 * 1024];
    const int64_t got =
        fs::read_fd(static_cast<uint32_t>(ofd), out_image, sizeof(out_image));
    (void)fs::close_fd(static_cast<uint32_t>(ofd));
    if (got < 64) {
        result.failure = 41;
        return false;
    }
    result.out_bytes = static_cast<uint32_t>(got);
    uint64_t entry = 0;
    uint32_t stage = 0;
    uint32_t failure = 0;
    if (!launch_user_elf(out_image, static_cast<size_t>(got), entry, stage,
                         failure)) {
        result.stage = 20 + stage;
        result.failure = 50 + failure;
        return false;
    }
    result.ran = 1;
    result.exit_code = syscall::session().exit_code;
    result.stage = 21;
    syscall::clear_session();
    if (result.exit_code != 42) {
        result.failure = 60;
        return false;
    }
    return true;
}

bool run_ide_rebuild(const char* source, size_t source_len,
                     const uint8_t* cc_image, size_t cc_size,
                     IdeRebuildResult& result) {
    result = {};
    result.stage = 1;
    uint32_t save_fail = 0;
    if (!ide_save_source(source, source_len, save_fail)) {
        result.failure = save_fail;
        result.src_bytes = static_cast<uint32_t>(source_len);
        return false;
    }
    result.saved = 1;
    result.src_bytes = static_cast<uint32_t>(source_len);
    result.stage = 2;
    if (!ide_build_and_run(cc_image, cc_size, result)) {
        result.saved = 1;
        return false;
    }
    result.saved = 1;
    return true;
}

}  // namespace cxc::user
