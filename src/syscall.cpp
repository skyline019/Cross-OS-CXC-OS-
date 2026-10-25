#include <cxc/syscall.hpp>

#include <cxc/fs.hpp>
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
constexpr size_t max_io = 256;
constexpr char expected_program_pass[] = "USER_PROGRAM_PASS\n";
constexpr char expected_fs_pass[] = "USER_FS_PASS\n";

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

bool user_pages_ok(uint64_t address, size_t length, bool need_write) {
    if (length == 0 || address == 0 || address >= user_canonical_limit ||
        length > max_io || address + length > user_canonical_limit) {
        return false;
    }
    const uint64_t root = read_cr3();
    uint64_t page = address & ~(page_size - 1);
    const uint64_t end = address + length;
    while (page < end) {
        const vmm::MappingInfo info = vmm::inspect_mapping_in(root, page);
        if (!info.present || !info.user) {
            return false;
        }
        if (need_write && !info.writable) {
            return false;
        }
        page += page_size;
    }
    return true;
}

void note_stdout_pass(const char* bytes, size_t length) {
    ++current.write_calls;
    current.write_bytes += static_cast<uint32_t>(length);
    if (length == sizeof(expected_program_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_program_pass) - 1;
             ++index) {
            if (bytes[index] != expected_program_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_program_pass = 1;
        }
    }
    if (length == sizeof(expected_fs_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_fs_pass) - 1;
             ++index) {
            if (bytes[index] != expected_fs_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_fs_pass = 1;
        }
    }
}

bool copy_user_path(uint64_t user_path, char* out, size_t capacity) {
    if (capacity == 0 || !user_pages_ok(user_path, 1, false)) {
        return false;
    }
    size_t length = 0;
    while (length + 1 < capacity) {
        if (!user_pages_ok(user_path + length, 1, false)) {
            return false;
        }
        const char value =
            *reinterpret_cast<const char*>(user_path + length);
        out[length] = value;
        if (value == '\0') {
            return true;
        }
        ++length;
        if (length >= fs::max_path) {
            return false;
        }
    }
    return false;
}

cpu::InterruptFrame* handle_write(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t buf = frame->rsi;
    const uint64_t len = frame->rdx;
    if (len == 0 || len > max_io ||
        !user_pages_ok(buf, static_cast<size_t>(len), false)) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const auto* bytes = reinterpret_cast<const char*>(buf);
    if (fd == fs::fd_stdout) {
        log::write(bytes, static_cast<size_t>(len));
        note_stdout_pass(bytes, static_cast<size_t>(len));
        frame->rax = len;
        return frame;
    }
    const int64_t wrote =
        fs::write_fd(static_cast<uint32_t>(fd), bytes, static_cast<size_t>(len));
    frame->rax = static_cast<uint64_t>(wrote);
    return frame;
}

cpu::InterruptFrame* handle_read(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t buf = frame->rsi;
    const uint64_t len = frame->rdx;
    if (len == 0 || len > max_io ||
        !user_pages_ok(buf, static_cast<size_t>(len), true)) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    auto* bytes = reinterpret_cast<char*>(buf);
    const int64_t got =
        fs::read_fd(static_cast<uint32_t>(fd), bytes, static_cast<size_t>(len));
    frame->rax = static_cast<uint64_t>(got);
    return frame;
}

cpu::InterruptFrame* handle_open(cpu::InterruptFrame* frame) {
    char path[fs::max_path]{};
    if (!copy_user_path(frame->rdi, path, sizeof(path))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const int64_t fd = fs::open_path(path, frame->rsi, frame->rdx);
    frame->rax = static_cast<uint64_t>(fd);
    return frame;
}

cpu::InterruptFrame* handle_close(cpu::InterruptFrame* frame) {
    const int64_t rc = fs::close_fd(static_cast<uint32_t>(frame->rdi));
    frame->rax = static_cast<uint64_t>(rc);
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
    fs::reset();
}

void begin_session(uint64_t resume_rip) {
    current = Session{};
    fs::reset();
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
    case sys_read:
        return handle_read(frame);
    case sys_write:
        return handle_write(frame);
    case sys_open:
        return handle_open(frame);
    case sys_close:
        return handle_close(frame);
    case sys_exit:
        return handle_exit(frame);
    default:
        frame->rax = static_cast<uint64_t>(-38);  // -ENOSYS
        return frame;
    }
}

}  // namespace cxc::syscall
