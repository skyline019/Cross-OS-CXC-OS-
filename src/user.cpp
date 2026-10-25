#include <cxc/user.hpp>

#include <cxc/cpu.hpp>
#include <cxc/elf.hpp>
#include <cxc/pmm.hpp>
#include <cxc/smp.hpp>
#include <cxc/syscall.hpp>
#include <cxc/user_hello_blob.hpp>
#include <cxc/vmm.hpp>

#include <stdint.h>

extern "C" void ring3_enter_iretq(uint64_t user_rip, uint64_t user_rsp);
extern "C" void ring3_after_user();

namespace cxc::user {
namespace {

ElfProbeResult probe{};
constexpr uint64_t user_stack_base = 0x0000000000402000ull;
constexpr uint64_t privilege_stack_base = 0xffffffff5e000000ull;
constexpr uint64_t privilege_stack_pages = 8;

bool ensure_tss() {
    const uint64_t stack_top =
        privilege_stack_base + privilege_stack_pages * pmm::page_size;
    // M37 maps this stack once; re-loading TR/RSP0 is idempotent for the BSP.
    return cpu::initialize_tss(stack_top);
}

}  // namespace

bool run_elf_probe(ElfProbeResult& result) {
    result = ElfProbeResult{};
    result.stage = 1;
    if (smp::current_cpu_index() != 0 || vmm::kernel_address_space() == 0) {
        result.failure = 1;
        return false;
    }
    if (!ensure_tss()) {
        result.failure = 2;
        return false;
    }
    result.stage = 2;

    const uint64_t user_root = vmm::create_user_address_space();
    if (user_root == 0) {
        result.failure = 3;
        return false;
    }

    elf::LoadResult loaded{};
    if (!elf::load_static_et_exec(user_root, hello_elf, hello_elf_size,
                                  loaded)) {
        (void)vmm::destroy_user_address_space(user_root);
        result.failure = 10 + loaded.failure;
        return false;
    }
    result.loaded = 1;
    result.entry = loaded.entry;
    result.stage = 3;

    const uint64_t stack_physical = pmm::allocate_pages(1);
    if (stack_physical == 0 ||
        !vmm::map_user_page(user_root, user_stack_base, stack_physical,
                            vmm::user_map_writable)) {
        if (stack_physical != 0) {
            pmm::free_pages(stack_physical, 1);
        }
        (void)vmm::destroy_user_address_space(user_root);
        result.failure = 4;
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
        result.failure = 5;
        return false;
    }

    ring3_enter_iretq(loaded.entry, user_rsp);
    result.entered = 1;

    if (!vmm::activate_address_space(kernel_root)) {
        cpu::restore_interrupts(irq_flags);
        result.failure = 6;
        return false;
    }
    cpu::restore_interrupts(irq_flags);
    result.stage = 4;

    const syscall::Session& session = syscall::session();
    if (session.exited == 0) {
        syscall::clear_session();
        (void)vmm::destroy_user_address_space(user_root);
        result.failure = 7;
        return false;
    }
    result.exited = 1;
    result.exit_code = session.exit_code;
    result.wrote_pass = session.saw_user_program_pass;
    result.stage = 5;

    if (result.exit_code != 0 || result.wrote_pass == 0 ||
        session.write_calls == 0) {
        syscall::clear_session();
        (void)vmm::destroy_user_address_space(user_root);
        result.failure = 8;
        return false;
    }

    if (!vmm::destroy_user_address_space(user_root)) {
        syscall::clear_session();
        result.failure = 9;
        return false;
    }
    syscall::clear_session();
    result.stage = 6;
    probe = result;
    return true;
}

const ElfProbeResult& elf_probe_result() {
    return probe;
}

}  // namespace cxc::user
