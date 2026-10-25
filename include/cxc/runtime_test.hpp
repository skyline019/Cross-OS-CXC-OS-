#pragma once

#include <cxc/cpu.hpp>

namespace cxc::runtime_test {

// Requires the base self-tests, SMP startup and APIC initialization to have
// completed. Exercises per-CPU scheduling, migration, SMP-safe allocators and
// runtime permission/TLB shootdown checks, then returns with interrupts off.
void run();

// Recover only the exact runtime read-only write probe. All other page faults
// remain fatal. The handler never logs or allocates and may run on any CPU.
bool on_page_fault(cpu::InterruptFrame& frame);

}  // namespace cxc::runtime_test
