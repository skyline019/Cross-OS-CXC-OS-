#pragma once

#include <stdint.h>

namespace cxc::ioapic {

constexpr uint64_t virtual_base = 0xffffffff40100000;
constexpr uint64_t virtual_stride = 0x200000;

// Maps every MADT IOAPIC and masks all redirection entries before IF is enabled.
bool initialize();

// Generic physical-destination routing used by the fixed IRQ registry.
// route_gsi leaves the line unmasked; mask_gsi preserves the other RTE bits.
bool route_gsi(uint32_t gsi, uint16_t flags, uint8_t vector);
bool mask_gsi(uint32_t gsi);

// Routes ISA IRQ0 through its MADT GSI override and proves delivery with a
// PIT channel-0 one-shot. The line is masked again before returning.
bool run_irq0_test();
void on_interrupt();

uint32_t redirection_entries();
uint32_t tested_gsi();
uint64_t test_interrupts();
bool initialized();

}  // namespace cxc::ioapic
