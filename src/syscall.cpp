#include <cxc/syscall.hpp>

#include <cxc/log.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" uint64_t ring3_saved_kernel_rsp;

namespace cxc::syscall {
namespace {

Session current{};
constexpr uint64_t user_canonical_limit = uint64_t{1} << 47;
constexpr uint64_t page_size = 4096;
constexpr size_t max_write = 256;
constexpr char expected_pass[] = "USER_PROGRAM_PASS\n";

void finish_to_kernel(cpu::InterruptFrame* frame) {
    frame->rip = current.resume_rip;
    frame->cs = cpu::kernel_cs;
    frame->rflags = 0x2;
    frame->rsp = ring3_saved_kernel_rsp;
    frame->ss = cpu::kernel_ss;
}

uint64_t read_cr3() {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

bool user_range_readable(uint64_t address, size_t length) {
    if (length == 0 || address == 0 || address >= user_canonical_limit ||
        length > max_write || address + length > user_canonical_limit) {
        return false;
    }
    const uint64_t root = read_cr3();
    uint64_t page = address & ~(page_size - 1);
    const uint64_t end = address + length;
    while (page < end) {
        // Walk the active (user) root — inspect_mapping always uses kernel CR3.
        const vmm::MappingInfo info = vmm::inspect_mapping_in(root, page);
        if (!info.present || !info.user) {
            return false;
        }
        page += page_size;
    }
    return true;
}

cpu::InterruptFrame* handle_write(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t buf = frame->rsi;
    const uint64_t len = frame->rdx;
    if (fd != 1 || len == 0 || len > max_write ||
        !user_range_readable(buf, static_cast<size_t>(len))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const auto* bytes = reinterpret_cast<const char*>(buf);
    log::write(bytes, static_cast<size_t>(len));
    ++current.write_calls;
    current.write_bytes += static_cast<uint32_t>(len);
    if (len == sizeof(expected_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_pass) - 1; ++index) {
            if (bytes[index] != expected_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_program_pass = 1;
        }
    }
    frame->rax = len;
    return frame;
}

cpu::InterruptFrame* handle_exit(cpu::InterruptFrame* frame) {
    current.exit_code = frame->rdi;
    current.exited = 1;
    current.active = 0;
    if (current.resume_rip == 0 || ring3_saved_kernel_rsp == 0) {
        log::panic("exit without user session resume");
    }
    finish_to_kernel(frame);
    return frame;
}

}  // namespace

void clear_session() {
    current = Session{};
}

void begin_session(uint64_t resume_rip) {
    current = Session{};
    current.resume_rip = resume_rip;
    current.active = 1;
}

const Session& session() {
    return current;
}

cpu::InterruptFrame* on_int80(cpu::InterruptFrame* frame) {
    if ((frame->cs & 3u) != 3u) {
        log::panic("int 0x80 from non-CPL3");
    }
    if (current.active == 0 || current.resume_rip == 0) {
        log::panic("unexpected int 0x80");
    }

    switch (frame->rax) {
    case sys_write:
        return handle_write(frame);
    case sys_exit:
        return handle_exit(frame);
    default:
        frame->rax = static_cast<uint64_t>(-38);  // -ENOSYS
        return frame;
    }
}

}  // namespace cxc::syscall
