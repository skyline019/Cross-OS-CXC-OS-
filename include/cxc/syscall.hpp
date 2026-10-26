#pragma once

#include <cxc/cpu.hpp>

#include <stdint.h>

namespace cxc::syscall {

// Linux x86-64 numbers (file + brk + mmap/munmap + mkdir).
constexpr uint64_t sys_read = 0;
constexpr uint64_t sys_write = 1;
constexpr uint64_t sys_open = 2;
constexpr uint64_t sys_close = 3;
constexpr uint64_t sys_stat = 4;
constexpr uint64_t sys_fstat = 5;
constexpr uint64_t sys_lseek = 8;
constexpr uint64_t sys_mmap = 9;
constexpr uint64_t sys_mprotect = 10;
constexpr uint64_t sys_munmap = 11;
constexpr uint64_t sys_brk = 12;
constexpr uint64_t sys_ioctl = 16;
constexpr uint64_t sys_readv = 19;
constexpr uint64_t sys_writev = 20;
constexpr uint64_t sys_pipe = 22;
constexpr uint64_t sys_dup = 32;
constexpr uint64_t sys_dup2 = 33;
constexpr uint64_t sys_fcntl = 72;
constexpr uint64_t sys_getcwd = 79;
constexpr uint64_t sys_chdir = 80;
constexpr uint64_t sys_getpid = 39;
constexpr uint64_t sys_fork = 57;
constexpr uint64_t sys_execve = 59;
constexpr uint64_t sys_exit = 60;
constexpr uint64_t sys_wait4 = 61;
constexpr uint64_t sys_exit_group = 231;
constexpr uint64_t sys_uname = 63;
constexpr uint64_t sys_arch_prctl = 158;
constexpr uint64_t sys_rename = 82;
constexpr uint64_t sys_mkdir = 83;
constexpr uint64_t sys_unlink = 87;
constexpr uint64_t sys_getppid = 110;
constexpr uint64_t sys_getdents64 = 217;
constexpr uint64_t sys_set_tid_address = 218;

// Linux arch_prctl codes (x86-64). Only FS is supported for user TLS.
constexpr uint64_t arch_set_gs = 0x1001;
constexpr uint64_t arch_set_fs = 0x1002;
constexpr uint64_t arch_get_fs = 0x1003;
constexpr uint64_t arch_get_gs = 0x1004;

constexpr uint64_t ring3_probe_magic = 0xc0de0001ull;

// Above musl-gcc fopen .data/.bss (M71 LIBFOP ends ~0x408bb0); stack at 0x420000.
constexpr uint64_t default_brk_base = 0x0000000000409000ull;
constexpr uint64_t brk_max_pages = 16;

// Anonymous mmap bump region (above brk window).
constexpr uint64_t default_mmap_base = 0x0000000000600000ull;
constexpr uint64_t mmap_max_pages = 16;

constexpr uint64_t prot_read = 1;
constexpr uint64_t prot_write = 2;
constexpr uint64_t map_private = 0x02;
constexpr uint64_t map_anonymous = 0x20;

struct Session {
    uint64_t resume_rip;
    uint64_t exit_code;
    uint64_t brk_base;
    uint64_t brk_current;
    uint64_t mmap_next;
    uint32_t exited;
    uint32_t write_calls;
    uint32_t write_bytes;
    uint32_t saw_user_program_pass;
    uint32_t saw_user_fs_pass;
    uint32_t saw_user_brk_pass;
    uint32_t saw_user_mmap_pass;
    uint32_t saw_user_dir_pass;
    uint32_t saw_user_munmap_pass;
    uint32_t saw_user_ext_elf_pass;
    uint32_t saw_user_cross_elf_pass;
    uint32_t saw_user_lseek_pass;
    uint32_t saw_user_fstat_pass;
    uint32_t saw_user_stat_pass;
    uint32_t saw_user_getdents_pass;
    uint32_t saw_user_arch_prctl_pass;
    uint32_t saw_user_libc_pass;
    uint32_t saw_user_libc_fs_pass;
    uint32_t saw_user_libc_sbrk_pass;
    uint32_t saw_user_libc_malloc_pass;
    uint32_t saw_user_libc_errno_pass;
    uint32_t saw_user_libc_string_pass;
    uint32_t saw_user_libc_writev_pass;
    uint32_t saw_user_libc_crt_pass;
    uint32_t saw_user_libc_uname_pass;
    uint32_t saw_user_libc_tid_pass;
    uint32_t saw_user_libc_pid_pass;
    uint32_t saw_user_musl_write_pass;
    uint32_t saw_user_musl_auxv_pass;
    uint32_t saw_user_musl_crt_pass;
    uint32_t saw_user_musl_malloc_pass;
    uint32_t saw_user_musl_printf_pass;
    uint32_t saw_user_musl_fopen_pass;
    uint32_t saw_user_musl_opendir_pass;
    uint32_t saw_user_musl_unlink_pass;
    uint32_t saw_user_musl_blk_pass;
    uint32_t saw_user_musl_dir_pass;
    uint32_t saw_disk_exec_pass;
    uint32_t saw_disk_argv_pass;
    uint32_t saw_disk_env_pass;
    uint32_t saw_fork_wait_pass;
    uint32_t saw_pipe_pass;
    uint32_t saw_chdir_pass;
    uint32_t saw_nest_pass;
    uint32_t saw_cap_pass;
    uint64_t user_root;
    uint64_t clear_child_tid;
    uint32_t pid;
    uint32_t ppid;
    uint32_t brk_grows;
    uint32_t brk_pages;
    uint32_t mmap_calls;
    uint32_t mmap_pages;
    uint32_t munmap_calls;
    uint32_t lseek_calls;
    uint32_t fstat_calls;
    uint32_t stat_calls;
    uint32_t getdents_calls;
    uint32_t arch_prctl_calls;
    uint32_t active;
};

void clear_session();
void begin_session(uint64_t resume_rip,
                   uint64_t brk_base = default_brk_base);
void set_user_root(uint64_t user_root);
void reset_heap_after_exec();
const Session& session();

cpu::InterruptFrame* on_int80(cpu::InterruptFrame* frame);

}  // namespace cxc::syscall
