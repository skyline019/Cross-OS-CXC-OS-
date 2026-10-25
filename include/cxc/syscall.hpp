#pragma once

#include <cxc/cpu.hpp>

#include <stdint.h>

namespace cxc::syscall {

// Linux x86-64 numbers (file + brk + mmap/munmap + mkdir).
constexpr uint64_t sys_read = 0;
constexpr uint64_t sys_write = 1;
constexpr uint64_t sys_open = 2;
constexpr uint64_t sys_close = 3;
constexpr uint64_t sys_fstat = 5;
constexpr uint64_t sys_lseek = 8;
constexpr uint64_t sys_mmap = 9;
constexpr uint64_t sys_munmap = 11;
constexpr uint64_t sys_brk = 12;
constexpr uint64_t sys_exit = 60;
constexpr uint64_t sys_mkdir = 83;

constexpr uint64_t ring3_probe_magic = 0xc0de0001ull;

constexpr uint64_t default_brk_base = 0x0000000000405000ull;
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
    uint32_t brk_grows;
    uint32_t brk_pages;
    uint32_t mmap_calls;
    uint32_t mmap_pages;
    uint32_t munmap_calls;
    uint32_t lseek_calls;
    uint32_t fstat_calls;
    uint32_t active;
};

void clear_session();
void begin_session(uint64_t resume_rip,
                   uint64_t brk_base = default_brk_base);
const Session& session();

cpu::InterruptFrame* on_int80(cpu::InterruptFrame* frame);

}  // namespace cxc::syscall
