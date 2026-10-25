#include <cxc/syscall.hpp>

#include <cxc/fs.hpp>
#include <cxc/log.hpp>
#include <cxc/pmm.hpp>
#include <cxc/user.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" uint64_t ring3_saved_kernel_rsp;
extern "C" void* memcpy(void*, const void*, size_t);

namespace cxc::syscall {
namespace {

Session current{};
constexpr uint64_t user_canonical_limit = uint64_t{1} << 47;
constexpr uint64_t page_size = 4096;
constexpr size_t max_io = 256;
// musl BUFSIZ is 1024; __stdio_read readv iov entries may be that large.
constexpr size_t max_iov_bytes = 4096;
constexpr char expected_program_pass[] = "USER_PROGRAM_PASS\n";
constexpr char expected_fs_pass[] = "USER_FS_PASS\n";
constexpr char expected_brk_pass[] = "USER_BRK_PASS\n";
constexpr char expected_mmap_pass[] = "USER_MMAP_PASS\n";
constexpr char expected_dir_pass[] = "USER_DIR_PASS\n";
constexpr char expected_munmap_pass[] = "USER_MUNMAP_PASS\n";
constexpr char expected_ext_elf_pass[] = "USER_EXT_ELF_PASS\n";
constexpr char expected_cross_elf_pass[] = "USER_CROSS_ELF_PASS\n";
constexpr char expected_lseek_pass[] = "USER_LSEEK_PASS\n";
constexpr char expected_fstat_pass[] = "USER_FSTAT_PASS\n";
constexpr char expected_stat_pass[] = "USER_STAT_PASS\n";
constexpr char expected_getdents_pass[] = "USER_GETDENTS_PASS\n";
constexpr char expected_arch_prctl_pass[] = "USER_ARCH_PRCTL_PASS\n";
constexpr char expected_libc_pass[] = "USER_LIBC_PASS\n";
constexpr char expected_libc_fs_pass[] = "USER_LIBC_FS_PASS\n";
constexpr char expected_libc_sbrk_pass[] = "USER_LIBC_SBRK_PASS\n";
constexpr char expected_libc_malloc_pass[] = "USER_LIBC_MALLOC_PASS\n";
constexpr char expected_libc_errno_pass[] = "USER_LIBC_ERRNO_PASS\n";
constexpr char expected_libc_string_pass[] = "USER_LIBC_STRING_PASS\n";
constexpr char expected_libc_writev_pass[] = "USER_LIBC_WRITEV_PASS\n";
constexpr char expected_libc_crt_pass[] = "USER_LIBC_CRT_PASS\n";
constexpr char expected_libc_uname_pass[] = "USER_LIBC_UNAME_PASS\n";
constexpr char expected_libc_tid_pass[] = "USER_LIBC_TID_PASS\n";
constexpr char expected_libc_pid_pass[] = "USER_LIBC_PID_PASS\n";
constexpr char expected_musl_write_pass[] = "USER_MUSL_PASS\n";
constexpr char expected_musl_auxv_pass[] = "USER_MUSL_AUXV_PASS\n";
constexpr char expected_musl_crt_pass[] = "USER_MUSL_CRT_PASS\n";
constexpr char expected_musl_malloc_pass[] = "USER_MUSL_MAL_PASS\n";
constexpr char expected_musl_printf_pass[] = "USER_MUSL_PRT_PASS\n";
constexpr char expected_musl_fopen_pass[] = "USER_MUSL_FOP_PASS\n";
constexpr char expected_musl_opendir_pass[] = "USER_MUSL_ODR_PASS\n";
constexpr char expected_musl_unlink_pass[] = "USER_MUSL_UNL_PASS\n";
constexpr char expected_musl_blk_pass[] = "USER_MUSL_BLK_PASS\n";
constexpr char expected_musl_dir_pass[] = "USER_MUSL_DIR_PASS\n";
constexpr char expected_disk_exec_pass[] = "DISK_EXEC_PASS\n";
constexpr char expected_disk_argv_pass[] = "DISK_ARGV_PASS\n";
constexpr char expected_disk_env_pass[] = "DISK_ENV_PASS\n";
constexpr int64_t errno_notty = -25;  // -ENOTTY
constexpr uint64_t fcntl_getfd = 1;
constexpr uint64_t fcntl_setfd = 2;
constexpr uint64_t fcntl_getfl = 3;
constexpr uint64_t fcntl_setfl = 4;
constexpr int64_t mmap_error = -1;
constexpr uint64_t probe_tid = 1;
constexpr uint64_t probe_pid = 1;
constexpr uint64_t probe_ppid = 0;
constexpr int64_t errno_inval = -22;
constexpr int64_t errno_fault = -14;
constexpr uint64_t max_iov = 8;

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
    // Cap at getdents max (largest user buffer we copy); callers still apply
    // their own tighter limits (e.g. max_io for read/write).
    if (length == 0 || address == 0 || address >= user_canonical_limit ||
        length > fs::max_getdents || address + length > user_canonical_limit) {
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
    if (length == sizeof(expected_brk_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_brk_pass) - 1;
             ++index) {
            if (bytes[index] != expected_brk_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_brk_pass = 1;
        }
    }
    if (length == sizeof(expected_mmap_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_mmap_pass) - 1;
             ++index) {
            if (bytes[index] != expected_mmap_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_mmap_pass = 1;
        }
    }
    if (length == sizeof(expected_dir_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_dir_pass) - 1;
             ++index) {
            if (bytes[index] != expected_dir_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_dir_pass = 1;
        }
    }
    if (length == sizeof(expected_munmap_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_munmap_pass) - 1;
             ++index) {
            if (bytes[index] != expected_munmap_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_munmap_pass = 1;
        }
    }
    if (length == sizeof(expected_ext_elf_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_ext_elf_pass) - 1;
             ++index) {
            if (bytes[index] != expected_ext_elf_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_ext_elf_pass = 1;
        }
    }
    if (length == sizeof(expected_cross_elf_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_cross_elf_pass) - 1;
             ++index) {
            if (bytes[index] != expected_cross_elf_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_cross_elf_pass = 1;
        }
    }
    if (length == sizeof(expected_lseek_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_lseek_pass) - 1;
             ++index) {
            if (bytes[index] != expected_lseek_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_lseek_pass = 1;
        }
    }
    if (length == sizeof(expected_fstat_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_fstat_pass) - 1;
             ++index) {
            if (bytes[index] != expected_fstat_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_fstat_pass = 1;
        }
    }
    if (length == sizeof(expected_stat_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_stat_pass) - 1;
             ++index) {
            if (bytes[index] != expected_stat_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_stat_pass = 1;
        }
    }
    if (length == sizeof(expected_getdents_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_getdents_pass) - 1;
             ++index) {
            if (bytes[index] != expected_getdents_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_getdents_pass = 1;
        }
    }
    if (length == sizeof(expected_arch_prctl_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_arch_prctl_pass) - 1;
             ++index) {
            if (bytes[index] != expected_arch_prctl_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_arch_prctl_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_fs_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_fs_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_fs_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_fs_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_sbrk_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_sbrk_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_sbrk_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_sbrk_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_malloc_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_malloc_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_malloc_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_malloc_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_errno_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_errno_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_errno_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_errno_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_string_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_string_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_string_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_string_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_writev_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_writev_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_writev_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_writev_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_crt_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_crt_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_crt_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_crt_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_uname_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_uname_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_uname_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_uname_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_tid_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_tid_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_tid_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_tid_pass = 1;
        }
    }
    if (length == sizeof(expected_libc_pid_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_libc_pid_pass) - 1;
             ++index) {
            if (bytes[index] != expected_libc_pid_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_libc_pid_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_write_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_write_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_write_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_write_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_auxv_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_auxv_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_auxv_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_auxv_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_crt_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_crt_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_crt_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_crt_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_malloc_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_malloc_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_malloc_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_malloc_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_printf_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_printf_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_printf_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_printf_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_fopen_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_fopen_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_fopen_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_fopen_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_opendir_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_opendir_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_opendir_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_opendir_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_unlink_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_unlink_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_unlink_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_unlink_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_blk_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_blk_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_blk_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_blk_pass = 1;
        }
    }
    if (length == sizeof(expected_musl_dir_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_musl_dir_pass) - 1;
             ++index) {
            if (bytes[index] != expected_musl_dir_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_user_musl_dir_pass = 1;
        }
    }
    if (length == sizeof(expected_disk_exec_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_disk_exec_pass) - 1;
             ++index) {
            if (bytes[index] != expected_disk_exec_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_disk_exec_pass = 1;
        }
    }
    if (length == sizeof(expected_disk_argv_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_disk_argv_pass) - 1;
             ++index) {
            if (bytes[index] != expected_disk_argv_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_disk_argv_pass = 1;
        }
    }
    if (length == sizeof(expected_disk_env_pass) - 1) {
        bool match = true;
        for (size_t index = 0; index < sizeof(expected_disk_env_pass) - 1;
             ++index) {
            if (bytes[index] != expected_disk_env_pass[index]) {
                match = false;
                break;
            }
        }
        if (match) {
            current.saw_disk_env_pass = 1;
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

bool grow_brk_to(uint64_t new_brk) {
    const uint64_t limit =
        current.brk_base + brk_max_pages * page_size;
    if (new_brk < current.brk_current || new_brk > limit ||
        (new_brk & (page_size - 1)) != 0) {
        return false;
    }
    const uint64_t root = read_cr3();
    uint64_t page = current.brk_current;
    while (page < new_brk) {
        const uint64_t physical = pmm::allocate_pages(1);
        if (physical == 0) {
            return false;
        }
        if (!vmm::map_user_page(root, page, physical, vmm::user_map_writable)) {
            pmm::free_pages(physical, 1);
            return false;
        }
        ++current.brk_pages;
        page += page_size;
    }
    if (new_brk > current.brk_current) {
        ++current.brk_grows;
    }
    current.brk_current = new_brk;
    return true;
}

cpu::InterruptFrame* handle_brk(cpu::InterruptFrame* frame) {
    const uint64_t request = frame->rdi;
    if (request == 0) {
        frame->rax = current.brk_current;
        return frame;
    }
    if (request < current.brk_base) {
        frame->rax = current.brk_current;
        return frame;
    }
    if (request == current.brk_current) {
        frame->rax = current.brk_current;
        return frame;
    }
    if (request < current.brk_current) {
        // Thin slice: no shrink; return current break (glibc-tolerant).
        frame->rax = current.brk_current;
        return frame;
    }
    if (!grow_brk_to(request)) {
        frame->rax = current.brk_current;
        return frame;
    }
    frame->rax = current.brk_current;
    return frame;
}

cpu::InterruptFrame* handle_mmap(cpu::InterruptFrame* frame) {
    // Linux x86-64: mmap(addr, len, prot, flags, fd, off) uses r10 for flags.
    const uint64_t addr = frame->rdi;
    const uint64_t length = frame->rsi;
    const uint64_t prot = frame->rdx;
    const uint64_t flags = frame->r10;
    const uint64_t fd = frame->r8;
    const uint64_t offset = frame->r9;

    const uint64_t anon_private = map_private | map_anonymous;
    if (addr != 0 || length == 0 || (length & (page_size - 1)) != 0 ||
        length > mmap_max_pages * page_size ||
        (prot & ~(prot_read | prot_write)) != 0 ||
        (prot & (prot_read | prot_write)) == 0 ||
        (flags & anon_private) != anon_private ||
        fd != static_cast<uint64_t>(-1) || offset != 0) {
        frame->rax = static_cast<uint64_t>(mmap_error);
        return frame;
    }

    const uint64_t limit = default_mmap_base + mmap_max_pages * page_size;
    if (current.mmap_next == 0 ||
        current.mmap_next + length > limit) {
        frame->rax = static_cast<uint64_t>(mmap_error);
        return frame;
    }

    const uint64_t root = read_cr3();
    const uint64_t base = current.mmap_next;
    uint64_t mapped = 0;
    while (mapped < length) {
        const uint64_t physical = pmm::allocate_pages(1);
        if (physical == 0) {
            frame->rax = static_cast<uint64_t>(mmap_error);
            return frame;
        }
        uint64_t map_flags = 0;
        if ((prot & prot_write) != 0) {
            map_flags |= vmm::user_map_writable;
        }
        if (!vmm::map_user_page(root, base + mapped, physical, map_flags)) {
            pmm::free_pages(physical, 1);
            frame->rax = static_cast<uint64_t>(mmap_error);
            return frame;
        }
        ++current.mmap_pages;
        mapped += page_size;
    }
    current.mmap_next = base + length;
    ++current.mmap_calls;
    frame->rax = base;
    return frame;
}

cpu::InterruptFrame* handle_munmap(cpu::InterruptFrame* frame) {
    const uint64_t addr = frame->rdi;
    const uint64_t length = frame->rsi;
    const uint64_t limit = default_mmap_base + mmap_max_pages * page_size;
    if (addr == 0 || length == 0 || (addr & (page_size - 1)) != 0 ||
        (length & (page_size - 1)) != 0 || addr < default_mmap_base ||
        addr + length > limit || addr + length > current.mmap_next) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }

    const uint64_t root = read_cr3();
    uint64_t page = addr;
    while (page < addr + length) {
        if (!vmm::unmap_user_page(root, page)) {
            frame->rax = static_cast<uint64_t>(-1);
            return frame;
        }
        if (current.mmap_pages > 0) {
            --current.mmap_pages;
        }
        page += page_size;
    }
    if (addr + length == current.mmap_next) {
        current.mmap_next = addr;
    }
    ++current.munmap_calls;
    frame->rax = 0;
    return frame;
}

constexpr size_t uts_field = 65;

struct UtsName {
    char sysname[uts_field];
    char nodename[uts_field];
    char release[uts_field];
    char version[uts_field];
    char machine[uts_field];
    char domainname[uts_field];
};

void fill_uts_field(char* dest, const char* src) {
    size_t index = 0;
    while (src[index] != '\0' && index + 1 < uts_field) {
        dest[index] = src[index];
        ++index;
    }
    while (index < uts_field) {
        dest[index++] = '\0';
    }
}

cpu::InterruptFrame* handle_uname(cpu::InterruptFrame* frame) {
    const uint64_t user_buf = frame->rdi;
    if (!user_pages_ok(user_buf, sizeof(UtsName), true)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }
    UtsName local{};
    fill_uts_field(local.sysname, "CXC/OS");
    fill_uts_field(local.nodename, "cxc");
    fill_uts_field(local.release, "0.83.0");
    fill_uts_field(local.version, "M82");
    fill_uts_field(local.machine, "x86_64");
    fill_uts_field(local.domainname, "");
    memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(user_buf)), &local,
           sizeof(local));
    frame->rax = 0;
    return frame;
}

cpu::InterruptFrame* handle_getpid(cpu::InterruptFrame* frame) {
    frame->rax = probe_pid;
    return frame;
}

cpu::InterruptFrame* handle_getppid(cpu::InterruptFrame* frame) {
    frame->rax = probe_ppid;
    return frame;
}

cpu::InterruptFrame* handle_set_tid_address(cpu::InterruptFrame* frame) {
    // Linux: always succeeds; returns caller's tid. Pointer may be NULL.
    const uint64_t tidptr = frame->rdi;
    if (tidptr != 0 && !user_pages_ok(tidptr, sizeof(int), true)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }
    current.clear_child_tid = tidptr;
    frame->rax = probe_tid;
    return frame;
}

// M71: musl stdio may tweak buffer page perms; mapped pages already RW/RX.
cpu::InterruptFrame* handle_mprotect(cpu::InterruptFrame* frame) {
    (void)frame->rdi;
    (void)frame->rsi;
    (void)frame->rdx;
    frame->rax = 0;
    return frame;
}

// M71: isatty → ioctl(TCGETS); treat all as non-tty.
cpu::InterruptFrame* handle_ioctl(cpu::InterruptFrame* frame) {
    (void)frame->rdi;
    (void)frame->rsi;
    (void)frame->rdx;
    frame->rax = static_cast<uint64_t>(errno_notty);
    return frame;
}

// M71: fopen O_CLOEXEC path uses F_SETFD; ignore FD flags.
cpu::InterruptFrame* handle_fcntl(cpu::InterruptFrame* frame) {
    const uint64_t cmd = frame->rsi;
    if (cmd == fcntl_getfd || cmd == fcntl_setfd || cmd == fcntl_getfl ||
        cmd == fcntl_setfl) {
        frame->rax = 0;
        return frame;
    }
    frame->rax = static_cast<uint64_t>(errno_inval);
    return frame;
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

struct UserIovec {
    uint64_t base;
    uint64_t len;
};

cpu::InterruptFrame* handle_writev(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t iov_addr = frame->rsi;
    const uint64_t iovcnt = frame->rdx;
    if (iovcnt == 0 || iovcnt > max_iov) {
        frame->rax = static_cast<uint64_t>(errno_inval);
        return frame;
    }
    const size_t table_bytes =
        static_cast<size_t>(iovcnt) * sizeof(UserIovec);
    if (!user_pages_ok(iov_addr, table_bytes, false)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }
    const auto* iov = reinterpret_cast<const UserIovec*>(iov_addr);

    // Static: MinGW freestanding has no ___chkstk_ms for large frames.
    static char scratch[max_iov_bytes];
    size_t total = 0;
    for (uint64_t index = 0; index < iovcnt; ++index) {
        const uint64_t base = iov[index].base;
        const uint64_t len = iov[index].len;
        if (len == 0) {
            continue;
        }
        if (len > max_iov_bytes ||
            total > max_iov_bytes - static_cast<size_t>(len) ||
            !user_pages_ok(base, static_cast<size_t>(len), false)) {
            frame->rax = static_cast<uint64_t>(errno_fault);
            return frame;
        }
        const auto* bytes = reinterpret_cast<const char*>(base);
        if (fd == fs::fd_stdout) {
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                scratch[total + i] = bytes[i];
            }
            total += static_cast<size_t>(len);
        } else {
            const int64_t wrote = fs::write_fd(
                static_cast<uint32_t>(fd), bytes, static_cast<size_t>(len));
            if (wrote < 0) {
                frame->rax = static_cast<uint64_t>(wrote);
                return frame;
            }
            total += static_cast<size_t>(wrote);
            if (static_cast<uint64_t>(wrote) < len) {
                break;
            }
        }
    }
    if (fd == fs::fd_stdout) {
        if (total == 0) {
            frame->rax = 0;
            return frame;
        }
        log::write(scratch, total);
        note_stdout_pass(scratch, total);
    }
    frame->rax = static_cast<uint64_t>(total);
    return frame;
}

// M71: musl __stdio_read uses readv with a 2-entry iovec (user buf + FILE buf).
cpu::InterruptFrame* handle_readv(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t iov_addr = frame->rsi;
    const uint64_t iovcnt = frame->rdx;
    if (iovcnt == 0 || iovcnt > max_iov) {
        frame->rax = static_cast<uint64_t>(errno_inval);
        return frame;
    }
    const size_t table_bytes =
        static_cast<size_t>(iovcnt) * sizeof(UserIovec);
    if (!user_pages_ok(iov_addr, table_bytes, false)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }
    const auto* iov = reinterpret_cast<const UserIovec*>(iov_addr);

    size_t total = 0;
    for (uint64_t index = 0; index < iovcnt; ++index) {
        const uint64_t base = iov[index].base;
        const uint64_t len = iov[index].len;
        if (len == 0) {
            continue;
        }
        if (len > max_iov_bytes ||
            total > max_iov_bytes - static_cast<size_t>(len) ||
            !user_pages_ok(base, static_cast<size_t>(len), true)) {
            frame->rax = static_cast<uint64_t>(errno_fault);
            return frame;
        }
        auto* bytes = reinterpret_cast<char*>(base);
        const int64_t got = fs::read_fd(static_cast<uint32_t>(fd), bytes,
                                        static_cast<size_t>(len));
        if (got < 0) {
            frame->rax = static_cast<uint64_t>(got);
            return frame;
        }
        total += static_cast<size_t>(got);
        if (static_cast<uint64_t>(got) < len) {
            break;
        }
    }
    frame->rax = static_cast<uint64_t>(total);
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

cpu::InterruptFrame* handle_lseek(cpu::InterruptFrame* frame) {
    const int64_t rc = fs::lseek_fd(static_cast<uint32_t>(frame->rdi),
                                    static_cast<int64_t>(frame->rsi),
                                    static_cast<int>(frame->rdx));
    if (rc >= 0) {
        ++current.lseek_calls;
    }
    frame->rax = static_cast<uint64_t>(rc);
    return frame;
}

cpu::InterruptFrame* handle_fstat(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t user_buf = frame->rsi;
    if (!user_pages_ok(user_buf, sizeof(fs::StatBuf), true)) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    fs::StatBuf local{};
    const int64_t rc = fs::fstat_fd(static_cast<uint32_t>(fd), local);
    if (rc != 0) {
        frame->rax = static_cast<uint64_t>(rc);
        return frame;
    }
    memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(user_buf)), &local,
           sizeof(local));
    ++current.fstat_calls;
    frame->rax = 0;
    return frame;
}

cpu::InterruptFrame* handle_stat(cpu::InterruptFrame* frame) {
    char path[fs::max_path]{};
    const uint64_t user_buf = frame->rsi;
    if (!copy_user_path(frame->rdi, path, sizeof(path)) ||
        !user_pages_ok(user_buf, sizeof(fs::StatBuf), true)) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    fs::StatBuf local{};
    const int64_t rc = fs::stat_path(path, local);
    if (rc != 0) {
        frame->rax = static_cast<uint64_t>(rc);
        return frame;
    }
    memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(user_buf)), &local,
           sizeof(local));
    ++current.stat_calls;
    frame->rax = 0;
    return frame;
}

cpu::InterruptFrame* handle_getdents64(cpu::InterruptFrame* frame) {
    const uint64_t fd = frame->rdi;
    const uint64_t buf = frame->rsi;
    const uint64_t len = frame->rdx;
    if (len == 0 || len > fs::max_getdents ||
        !user_pages_ok(buf, static_cast<size_t>(len), true)) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const int64_t rc = fs::getdents64_fd(
        static_cast<uint32_t>(fd),
        reinterpret_cast<void*>(static_cast<uintptr_t>(buf)),
        static_cast<size_t>(len));
    if (rc >= 0) {
        ++current.getdents_calls;
    }
    frame->rax = static_cast<uint64_t>(rc);
    return frame;
}

cpu::InterruptFrame* handle_arch_prctl(cpu::InterruptFrame* frame) {
    const uint64_t code = frame->rdi;
    const uint64_t arg = frame->rsi;
    constexpr uint64_t user_canonical_limit = uint64_t{1} << 47;

    if (code == arch_set_fs) {
        // Allow 0 (clear) or a canonical user address.
        if (arg != 0 && arg >= user_canonical_limit) {
            frame->rax = static_cast<uint64_t>(errno_inval);
            return frame;
        }
        cpu::write_msr(cpu::ia32_fs_base, arg);
        ++current.arch_prctl_calls;
        frame->rax = 0;
        return frame;
    }
    if (code == arch_get_fs) {
        if (!user_pages_ok(arg, sizeof(uint64_t), true)) {
            frame->rax = static_cast<uint64_t>(errno_inval);
            return frame;
        }
        const uint64_t value = cpu::read_msr(cpu::ia32_fs_base);
        *reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(arg)) = value;
        ++current.arch_prctl_calls;
        frame->rax = 0;
        return frame;
    }
    // SET/GET_GS rejected: kernel owns GS for per-CPU (P-40).
    frame->rax = static_cast<uint64_t>(errno_inval);
    return frame;
}

cpu::InterruptFrame* handle_mkdir(cpu::InterruptFrame* frame) {
    char path[fs::max_path]{};
    if (!copy_user_path(frame->rdi, path, sizeof(path))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const int64_t rc = fs::mkdir_path(path, frame->rsi);
    frame->rax = static_cast<uint64_t>(rc);
    return frame;
}

cpu::InterruptFrame* handle_unlink(cpu::InterruptFrame* frame) {
    char path[fs::max_path]{};
    if (!copy_user_path(frame->rdi, path, sizeof(path))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    const int64_t rc = fs::unlink_path(path);
    frame->rax = static_cast<uint64_t>(rc);
    return frame;
}

bool copy_user_string_vector(uint64_t user_vec, char storage[][user::max_exec_arg_bytes],
                             const char** ptrs, uint32_t max_count,
                             uint32_t& out_count) {
    out_count = 0;
    if (user_vec == 0) {
        return true;
    }
    for (; out_count < max_count; ++out_count) {
        const uint64_t slot = user_vec + out_count * sizeof(uint64_t);
        if (!user_pages_ok(slot, sizeof(uint64_t), false)) {
            return false;
        }
        uint64_t ptr = 0;
        memcpy(&ptr, reinterpret_cast<const void*>(slot), sizeof(ptr));
        if (ptr == 0) {
            break;
        }
        if (!copy_user_path(ptr, storage[out_count], user::max_exec_arg_bytes)) {
            return false;
        }
        ptrs[out_count] = storage[out_count];
    }
    return true;
}

cpu::InterruptFrame* handle_rename(cpu::InterruptFrame* frame) {
    char old_path[fs::max_path]{};
    char new_path[fs::max_path]{};
    if (!copy_user_path(frame->rdi, old_path, sizeof(old_path)) ||
        !copy_user_path(frame->rsi, new_path, sizeof(new_path))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }
    frame->rax = static_cast<uint64_t>(fs::rename_path(old_path, new_path));
    return frame;
}

cpu::InterruptFrame* handle_execve(cpu::InterruptFrame* frame) {
    char path[fs::max_path]{};
    if (!copy_user_path(frame->rdi, path, sizeof(path))) {
        frame->rax = static_cast<uint64_t>(-1);
        return frame;
    }

    // M81/M82: copy argv/envp from the old address space before teardown.
    char arg_storage[user::max_exec_argc][user::max_exec_arg_bytes]{};
    const char* arg_ptrs[user::max_exec_argc]{};
    uint32_t argc = 0;
    if (!copy_user_string_vector(frame->rsi, arg_storage, arg_ptrs,
                                 user::max_exec_argc, argc)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }

    char env_storage[user::max_exec_envc][user::max_exec_arg_bytes]{};
    const char* env_ptrs[user::max_exec_envc]{};
    uint32_t envc = 0;
    if (!copy_user_string_vector(frame->rdx, env_storage, env_ptrs,
                                 user::max_exec_envc, envc)) {
        frame->rax = static_cast<uint64_t>(errno_fault);
        return frame;
    }

    uint32_t failure = 0;
    if (!user::exec_replace(path, frame, failure, arg_ptrs, argc, env_ptrs,
                            envc)) {
        frame->rax = static_cast<uint64_t>(-failure);
        return frame;
    }
    // Success: frame already retargeted; do not touch rax as return value
    // for the old image (never resumes).
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

void begin_session(uint64_t resume_rip, uint64_t brk_base) {
    current = Session{};
    fs::reset();
    current.resume_rip = resume_rip;
    current.brk_base = brk_base;
    current.brk_current = brk_base;
    current.mmap_next = default_mmap_base;
    current.active = 1;
}

void set_user_root(uint64_t user_root) {
    current.user_root = user_root;
}

void reset_heap_after_exec() {
    current.brk_base = default_brk_base;
    current.brk_current = default_brk_base;
    current.mmap_next = default_mmap_base;
    current.brk_grows = 0;
    current.brk_pages = 0;
    current.mmap_calls = 0;
    current.mmap_pages = 0;
    current.clear_child_tid = 0;
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
    case sys_readv:
        return handle_readv(frame);
    case sys_writev:
        return handle_writev(frame);
    case sys_open:
        return handle_open(frame);
    case sys_close:
        return handle_close(frame);
    case sys_stat:
        return handle_stat(frame);
    case sys_fstat:
        return handle_fstat(frame);
    case sys_lseek:
        return handle_lseek(frame);
    case sys_mmap:
        return handle_mmap(frame);
    case sys_mprotect:
        return handle_mprotect(frame);
    case sys_munmap:
        return handle_munmap(frame);
    case sys_brk:
        return handle_brk(frame);
    case sys_ioctl:
        return handle_ioctl(frame);
    case sys_fcntl:
        return handle_fcntl(frame);
    case sys_arch_prctl:
        return handle_arch_prctl(frame);
    case sys_rename:
        return handle_rename(frame);
    case sys_mkdir:
        return handle_mkdir(frame);
    case sys_unlink:
        return handle_unlink(frame);
    case sys_getdents64:
        return handle_getdents64(frame);
    case sys_execve:
        return handle_execve(frame);
    case sys_exit:
    case sys_exit_group:
        return handle_exit(frame);
    case sys_uname:
        return handle_uname(frame);
    case sys_set_tid_address:
        return handle_set_tid_address(frame);
    case sys_getpid:
        return handle_getpid(frame);
    case sys_getppid:
        return handle_getppid(frame);
    default:
        frame->rax = static_cast<uint64_t>(-38);  // -ENOSYS
        return frame;
    }
}

}  // namespace cxc::syscall
