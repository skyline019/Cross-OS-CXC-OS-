#include <cxc/user.hpp>
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
uint64_t seed_linux_user_stack(uint8_t* page, uint64_t page_va,
                               uint64_t entry, uint64_t phdr_va,
                               uint64_t phent, uint64_t phnum,
                               const char* const* argv, uint32_t argc,
                               const char* const* envp, uint32_t envc) {
    memset(page, 0, pmm::page_size);
    uint8_t* cursor = page + pmm::page_size;

    auto push_bytes = [&](const void* src, size_t length) -> uint64_t {
        cursor -= length;
        memcpy(cursor, src, length);
        return page_va + static_cast<uint64_t>(cursor - page);
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

    return page_va + static_cast<uint64_t>(cursor - page);
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
/* M90: above brk (≤0x509000) and mmap (≤0x700000) windows. */
constexpr uint64_t user_stack_base = 0x0000000000800000ull;
// A2 mini-cc needs more than one page (parse/emit frames); 32 KiB floor.
constexpr uint64_t user_stack_pages = 8;
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
                     uint32_t& stage, uint32_t& failure) {
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
    auto* const stack_page = reinterpret_cast<uint8_t*>(
        vmm::direct_map_base + stack_physical +
        (user_stack_pages - 1) * pmm::page_size);
    const uint64_t user_rsp = seed_linux_user_stack(
        stack_page, top_page_va, loaded.entry, loaded.at_phdr,
        loaded.at_phent, loaded.at_phnum, nullptr, 0, nullptr, 0);
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
    static uint8_t image[cxcfs::max_file_bytes];
    memset(image, 0, sizeof(image));
    const int64_t got =
        fs::read_fd(static_cast<uint32_t>(fd), image, sizeof(image));
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

    auto* const stack_page = reinterpret_cast<uint8_t*>(
        vmm::direct_map_base + stack_physical +
        (user_stack_pages - 1) * pmm::page_size);
    const uint64_t user_rsp = seed_linux_user_stack(
        stack_page, top_page_va, loaded.entry, loaded.at_phdr,
        loaded.at_phent, loaded.at_phnum, argv, argc, envp, envc);

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
                        OnboxCcProbeResult& result) {
    // M114: C-decl fns in self ET_REL + SysV internal call; edge DMSG+DHINT.
    static const char lib_text[] =
        "CXC_A2\n"
        "int add(int a, int b){\n"
        "return a+b;\n"
        "}\n"
        "int bump(int x){\n"
        "return add(x, 7);\n"
        "}\n";
    static const char src_text[] =
        "CXC_A2\n"
        "#lib \"lib.o\"\n"
        "#lib \"io.elf\"\n"
        "import fn bump(x);\n"
        "import fn cwrite(p,n);\n"
        "import fn strlen(s);\n"
        "int main(void){\n"
        "int n;\n"
        "int m;\n"
        "m=strlen(\"WMSG:CF\\n\");\n"
        "cwrite(\"WMSG:CF\\n\", m);\n"
        "n=bump(35);\n"
        "print n;\n"
        "return n;\n"
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
        const int64_t lsrc = fs::open_path(
            "disk/lib.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
        if (lsrc < 0) {
            result.failure = 46;
            return false;
        }
        const size_t lib_len = sizeof(lib_text) - 1;
        if (fs::write_fd(static_cast<uint32_t>(lsrc), lib_text, lib_len) !=
            static_cast<int64_t>(lib_len)) {
            (void)fs::close_fd(static_cast<uint32_t>(lsrc));
            result.failure = 47;
            return false;
        }
        if (fs::close_fd(static_cast<uint32_t>(lsrc)) != 0) {
            result.failure = 48;
            return false;
        }
    }

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
    result.wrote_cc = 1;
    result.stage = 3;

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
    result.stage = 5;

    if (result.exit_code != 42 || result.wrote_pass == 0) {
        syscall::clear_session();
        result.failure = 10;
        return false;
    }
    syscall::clear_session();

    // Edge: cross-#lib UND (crt without rt) → exit 4 + DMSG; drop lib.c so
    // the driver does not re-emit lib.o on this pass.
    result.stage = 6;
    (void)fs::unlink_path("disk/lib.c");
    const int64_t bfd = fs::open_path(
        "disk/in.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
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
    onbox_cc_probe = result;
    return true;
}

const OnboxCcProbeResult& onbox_cc_probe_result() {
    return onbox_cc_probe;
}

}  // namespace cxc::user
