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
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void ring3_enter_iretq(uint64_t user_rip, uint64_t user_rsp);
extern "C" void ring3_after_user();

namespace cxc::user {
namespace {

ElfProbeResult elf_probe{};
FsProbeResult fs_probe{};
BrkProbeResult brk_probe{};
MmapProbeResult mmap_probe{};
DirProbeResult dir_probe{};
constexpr uint64_t user_stack_base = 0x0000000000402000ull;
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
    const uint64_t user_rsp = user_stack_base + pmm::page_size;
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

}  // namespace cxc::user
