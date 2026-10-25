#pragma once

#include <cxc/cpu.hpp>

#include <stdint.h>

namespace cxc::syscall {

// Linux x86-64 numbers (v0→v1 file + brk).
constexpr uint64_t sys_read = 0;
constexpr uint64_t sys_write = 1;
constexpr uint64_t sys_open = 2;
constexpr uint64_t sys_close = 3;
constexpr uint64_t sys_brk = 12;
constexpr uint64_t sys_exit = 60;

constexpr uint64_t ring3_probe_magic = 0xc0de0001ull;

// Default user heap base: above ELF (0x400000) and stack (0x402000).
constexpr uint64_t default_brk_base = 0x0000000000405000ull;
constexpr uint64_t brk_max_pages = 16;

struct Session {
    uint64_t resume_rip;
    uint64_t exit_code;
    uint64_t brk_base;
    uint64_t brk_current;
    uint32_t exited;
    uint32_t write_calls;
    uint32_t write_bytes;
    uint32_t saw_user_program_pass;
    uint32_t saw_user_fs_pass;
    uint32_t saw_user_brk_pass;
    uint32_t brk_grows;
    uint32_t brk_pages;
    uint32_t active;
};

void clear_session();
void begin_session(uint64_t resume_rip,
                   uint64_t brk_base = default_brk_base);
const Session& session();

cpu::InterruptFrame* on_int80(cpu::InterruptFrame* frame);

}  // namespace cxc::syscall
