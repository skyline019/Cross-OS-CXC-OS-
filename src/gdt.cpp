#include <cxc/cpu.hpp>

#include <stdint.h>

namespace {

struct Gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Flat long-mode kernel code/data descriptors. The far return in gdt_load
// reloads CS; the remaining data segments are reloaded explicitly.
alignas(8) uint64_t gdt[] = {
    0x0000000000000000,
    0x00af9a000000ffff,  // 0x08: 64-bit code, ring 0.
    0x00cf92000000ffff,  // 0x10: data, ring 0.
};

Gdtr gdtr = {static_cast<uint16_t>(sizeof(gdt) - 1),
             reinterpret_cast<uint64_t>(gdt)};

}  // namespace

extern "C" void gdt_load(const Gdtr* descriptor);

namespace cxc::cpu {

void initialize_gdt() {
    gdt_load(&gdtr);
}

}  // namespace cxc::cpu
