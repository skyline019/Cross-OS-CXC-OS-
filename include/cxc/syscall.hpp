#pragma once

#include <cxc/cpu.hpp>

#include <stdint.h>

namespace cxc::syscall {

// Linux x86-64 numbers (v0 surface: write + exit only).
constexpr uint64_t sys_write = 1;
constexpr uint64_t sys_exit = 60;

constexpr uint64_t ring3_probe_magic = 0xc0de0001ull;

struct Session {
    uint64_t resume_rip;
    uint64_t exit_code;
    uint32_t exited;
    uint32_t write_calls;
    uint32_t write_bytes;
    uint32_t saw_user_program_pass;
    uint32_t active;
};

void clear_session();
void begin_session(uint64_t resume_rip);
const Session& session();

// Called from the IDT path for CPL3 `int 0x80`. May rewrite the frame to
// resume in kernel (exit / M37 probe) or return to user (write).
cpu::InterruptFrame* on_int80(cpu::InterruptFrame* frame);

}  // namespace cxc::syscall
