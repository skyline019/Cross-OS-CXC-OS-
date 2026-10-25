#include <cxc/cpu.hpp>

#include <stddef.h>
#include <stdint.h>

namespace {

struct Gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct Tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed));

static_assert(sizeof(Tss64) == 104);

// Flat long-mode descriptors. Indices 5–6 hold the 16-byte TSS system segment.
alignas(8) uint64_t gdt[7] = {
    0x0000000000000000,
    0x00af9a000000ffff,  // 0x08: 64-bit code, ring 0.
    0x00cf92000000ffff,  // 0x10: data, ring 0.
    0x00cff2000000ffff,  // 0x18: data, ring 3 (SS=0x1b; STAR-compatible).
    0x00affa000000ffff,  // 0x20: 64-bit code, ring 3 (CS=0x23).
    0x0000000000000000,  // 0x28: TSS low (filled by initialize_tss).
    0x0000000000000000,  // TSS high.
};

// Kernel stack top for SYSCALL entry (SYSCALL does not load TSS.RSP0).
extern "C" {
uint64_t syscall_kernel_rsp = 0;
void syscall_entry();
}

alignas(16) Tss64 bsp_tss{};

Gdtr gdtr = {static_cast<uint16_t>(sizeof(gdt) - 1),
             reinterpret_cast<uint64_t>(gdt)};

void encode_tss_descriptor(uint64_t* slot, uint64_t base, uint32_t limit) {
    slot[0] = (static_cast<uint64_t>(limit) & 0xffffull) |
              ((base & 0xffffull) << 16) |
              (((base >> 16) & 0xffull) << 32) |
              (0x89ull << 40) |
              (((static_cast<uint64_t>(limit) >> 16) & 0xfull) << 48) |
              (((base >> 24) & 0xffull) << 56);
    slot[1] = (base >> 32) & 0xffffffffull;
}

}  // namespace

extern "C" void gdt_load(const Gdtr* descriptor);
extern "C" void tss_load(uint16_t selector);

namespace cxc::cpu {

void initialize_gdt() {
    gdt_load(&gdtr);
}

bool initialize_tss(uint64_t privilege_stack_top) {
    if (privilege_stack_top == 0 || (privilege_stack_top & 0xfull) != 0) {
        return false;
    }
    bsp_tss = Tss64{};
    bsp_tss.rsp0 = privilege_stack_top;
    bsp_tss.iopb = static_cast<uint16_t>(sizeof(Tss64));
    encode_tss_descriptor(&gdt[5], reinterpret_cast<uint64_t>(&bsp_tss),
                          static_cast<uint32_t>(sizeof(Tss64) - 1));
    // Reload GDTR so the CPU observes the new TSS descriptor limit/base.
    gdt_load(&gdtr);
    tss_load(tss_selector);

    // M66: arm SYSCALL. STAR_USER=0x10 → SYSRET SS=0x1b, CS=0x23.
    syscall_kernel_rsp = privilege_stack_top;
    write_msr(ia32_efer, read_msr(ia32_efer) | efer_sce);
    write_msr(ia32_star, (0x10ull << 48) | (static_cast<uint64_t>(kernel_cs) << 32));
    write_msr(ia32_lstar, reinterpret_cast<uint64_t>(&syscall_entry));
    write_msr(ia32_sfmask, sfmask_default);
    return true;
}

}  // namespace cxc::cpu
