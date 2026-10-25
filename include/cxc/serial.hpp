#pragma once

#include <stdint.h>

namespace cxc::serial {

void initialize();
void write(char value);
bool present();

// Uses the 16550 loopback mode and a real ISA IRQ4 IOAPIC route. Logging must
// remain outside this function until normal MCR/FIFO state has been restored.
bool run_irq_loopback_test(uint32_t& gsi, uint8_t& vector);
uint64_t irq_loopback_calls();

}  // namespace cxc::serial
