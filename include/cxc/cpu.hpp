#pragma once

#include <stdint.h>

namespace cxc::cpu {

constexpr uint64_t timer_vector = 32;
constexpr uint64_t ioapic_test_vector = 33;
constexpr uint64_t external_vector_begin = 34;
constexpr uint64_t external_vector_end = 48;
constexpr uint64_t ipi_vector = 48;
constexpr uint64_t scheduler_ipi_vector = 49;
constexpr uint64_t scheduler_wakeup_vector = 50;
constexpr uint64_t tlb_shootdown_vector = 51;
constexpr uint64_t ring3_syscall_vector = 0x80;  // M37: int 0x80 from CPL3
constexpr uint64_t spurious_vector = 255;

constexpr uint16_t kernel_cs = 0x08;
constexpr uint16_t kernel_ss = 0x10;
// M66: Linux-like order (user data then user code) so STAR/SYSRET match:
// SS=0x1b, CS=0x23 with STAR_USER=0x10.
constexpr uint16_t user_ss = 0x1b;
constexpr uint16_t user_cs = 0x23;
constexpr uint16_t tss_selector = 0x28;

// Matches interrupts.S. In 64-bit mode the CPU saves/restores SS:RSP
// unconditionally, even without a privilege transition. The stub supplies
// vector/error and the common entry supplies fifteen general registers.
struct InterruptFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};
static_assert(sizeof(InterruptFrame) == 176);

void initialize_gdt();
// BSP-only: install TSS descriptor, load TR, set RSP0 (M37). Safe to call
// once after higher-half GDT is active. APs must not call this. Also arms
// SYSCALL MSRs once RSP0 is known (M66).
bool initialize_tss(uint64_t privilege_stack_top);
void initialize_idt();
// Loads the already initialized shared IDT on an application processor.
void load_idt();
bool breakpoint_was_seen();
bool ring3_int80_was_seen();
void ring3_clear_int80_seen();
void ring3_set_resume(uint64_t rip);

inline uint64_t read_flags() {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
    return flags;
}

inline bool interrupts_enabled() {
    return (read_flags() & 0x200) != 0;
}

inline void disable_interrupts() {
    __asm__ volatile("cli" : : : "memory");
}

inline void enable_interrupts() {
    __asm__ volatile("sti" : : : "memory");
}

inline void restore_interrupts(uint64_t saved_flags) {
    if ((saved_flags & 0x200) != 0) {
        enable_interrupts();
    }
}

inline void relax() {
    __asm__ volatile("pause" : : : "memory");
}

inline void wait_for_interrupt() {
    __asm__ volatile("hlt" : : : "memory");
}

inline uint64_t read_tsc() {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return (static_cast<uint64_t>(high) << 32) | low;
}

// x86-64 FS/GS base MSRs (used by arch_prctl for user TLS; do not SET_GS —
// the kernel owns IA32_GS_BASE for per-CPU data).
constexpr uint32_t ia32_fs_base = 0xC0000100;
constexpr uint32_t ia32_gs_base = 0xC0000101;
// M66: SYSCALL/SYSRET MSRs (LSTAR path; int 0x80 remains supported).
constexpr uint32_t ia32_efer = 0xC0000080;
constexpr uint32_t ia32_star = 0xC0000081;
constexpr uint32_t ia32_lstar = 0xC0000082;
constexpr uint32_t ia32_sfmask = 0xC0000084;
constexpr uint64_t efer_sce = 1ull;  // System Call Extensions
// SFMASK clears IF|TF|DF on SYSCALL (parity with interrupt-gate IF clear).
constexpr uint64_t sfmask_default = 0x700ull;

inline uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

inline void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(static_cast<uint32_t>(value)),
                       "d"(static_cast<uint32_t>(value >> 32))
                     : "memory");
}

[[noreturn]] inline void halt() {
    for (;;) {
        __asm__ volatile("cli; hlt" : : : "memory");
    }
}

}  // namespace cxc::cpu
