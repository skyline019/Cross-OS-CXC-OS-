#pragma once

#include <stdint.h>

namespace cxc::apic {

constexpr uint64_t local_apic_virtual_base = 0xffffffff40000000;

// Initializes x2APIC MSRs when supported, otherwise xAPIC MMIO, and calibrates
// the local timer against PIT channel 2. IF stays clear until start_periodic().
bool initialize();
// Initializes an AP's local APIC in the BSP's existing xAPIC/x2APIC mode.
// The AP timer remains masked and no global calibration state is changed.
bool initialize_application_processor(uint32_t expected_apic_id);
void start_periodic();
void stop_periodic();
void end_of_interrupt();
void note_spurious();

uint32_t apic_id();
uint32_t current_apic_id();
bool send_init_ipi(uint32_t destination_apic_id);
bool send_startup_ipi(uint32_t destination_apic_id, uint8_t page);
bool send_fixed_ipi(uint32_t destination_apic_id, uint8_t vector);
uint32_t calibration_ticks();
uint32_t periodic_ticks();
uint64_t spurious_interrupts();
bool initialized();
bool uses_x2apic();
const char* mode_name();

}  // namespace cxc::apic
