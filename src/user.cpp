#include <cxc/user.hpp>

#include <cxc/cpu.hpp>
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

// M67: seed argc/argv/envp/auxv at the top of the user stack page so musl
// `_start` (`mov %rsp,%rdi`) can read a Linux-shaped process image.
uint64_t seed_linux_user_stack(uint8_t* page, uint64_t page_va,
                               uint64_t entry, uint64_t phdr_va,
                               uint64_t phent, uint64_t phnum) {
    memset(page, 0, pmm::page_size);
    uint8_t* cursor = page + pmm::page_size;

    auto push_bytes = [&](const void* src, size_t length) -> uint64_t {
        cursor -= length;
        memcpy(cursor, src, length);
        return page_va + static_cast<uint64_t>(cursor - page);
    };

    static const char arg0[] = "musl";
    const uint64_t arg0_va = push_bytes(arg0, sizeof(arg0));
    uint8_t random[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint64_t random_va = push_bytes(random, sizeof(random));

    // Align vector area so final RSP is 16-byte aligned (SysV AMD64).
    cursor = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(cursor) & ~uintptr_t{0xfull});

    auto push_u64 = [&](uint64_t value) {
        cursor -= 8;
        memcpy(cursor, &value, 8);
    };

    // Highest: auxv pairs (AT_NULL last), then envp NULL, argv, argc.
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

    push_u64(0);         // envp terminator
    push_u64(0);         // argv terminator
    push_u64(arg0_va);   // argv[0]
    push_u64(1);         // argc

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
/* Above brk window (0x409000 + 16 pages → 0x419000) and musl .data. */
constexpr uint64_t user_stack_base = 0x0000000000420000ull;
constexpr uint64_t privilege_stack_base = 0xffffffff5e000000ull;
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

    const uint64_t stack_physical = pmm::allocate_pages(1);
    if (stack_physical == 0 ||
        !vmm::map_user_page(user_root, user_stack_base, stack_physical,
                            vmm::user_map_writable)) {
        if (stack_physical != 0) {
            pmm::free_pages(stack_physical, 1);
        }
        (void)vmm::destroy_user_address_space(user_root);
        failure = 4;
        return false;
    }

    const uint64_t kernel_root = vmm::kernel_address_space();
    auto* const stack_page = reinterpret_cast<uint8_t*>(
        vmm::direct_map_base + stack_physical);
    const uint64_t user_rsp = seed_linux_user_stack(
        stack_page, user_stack_base, loaded.entry, loaded.at_phdr,
        loaded.at_phent, loaded.at_phnum);
    syscall::begin_session(reinterpret_cast<uint64_t>(&ring3_after_user));

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

    if (syscall::session().exited == 0) {
        syscall::clear_session();
        (void)vmm::destroy_user_address_space(user_root);
        failure = 7;
        return false;
    }
    stage = 5;

    if (!vmm::destroy_user_address_space(user_root)) {
        syscall::clear_session();
        failure = 9;
        return false;
    }
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

}  // namespace cxc::user
