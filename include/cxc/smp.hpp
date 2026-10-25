#pragma once

#include <cxc/acpi.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::smp {

constexpr size_t maximum_processors = acpi::maximum_processors;
constexpr uint64_t application_stack_pages = 8;
constexpr uint64_t application_stack_region = 0xffffffff60000000;
constexpr uint64_t application_stack_stride = 0x200000;

struct CpuResult {
    uint32_t apic_id;
    uint32_t online;
    uint32_t parked;
    uint32_t error;
    uint64_t ipi_calls;
    uint64_t last_ipi_payload;
    uint64_t stack_physical;
    uint64_t stack_base;
};

struct TestInfo {
    uint32_t stage;
    uint32_t expected_processors;
    uint32_t online_processors;
    uint32_t second_sipi_count;
    uint32_t single_cpu;
    uint32_t active_cpu;
    uint64_t total_ipi_calls;
    uint64_t trampoline_physical;
    CpuResult cpus[maximum_processors];
};

// Copies the flat blob while firmware's low identity mappings still exist.
// The two pages were allocated as EfiLoaderData before ExitBootServices.
bool prepare_trampoline(uint64_t code_physical);

// Runs on the BSP with IF clear, after its LAPIC is initialized and before
// the IOAPIC/scheduler tests. APs first enter a parked wait loop.
bool run();

// Vector 49 wakes the AP park loop for scheduler start/stop. It is separate
// from the vector-48 delivery evidence and never schedules in interrupt context.
bool signal_application_processors();
// Sends a targeted fixed vector to any online CPU, including the BSP itself.
// Used by scheduler wakeup (50) and TLB shootdown (51), never as delivery proof.
bool signal_processor(uint32_t cpu_index, uint8_t vector);
uint64_t scheduler_control_ipi_count(uint32_t cpu_index);
uint32_t apic_id_for_cpu(uint32_t cpu_index);

// Called with IF clear after all device/scheduler regressions. APs have
// returned from their per-CPU scheduler and must answer one more vector-48 IPI.
bool verify_parked_processors();

const TestInfo& test_info();
uint32_t current_cpu_index();
uint32_t online_processor_count();

// Vector-48/49 handlers. They only touch the current GS-based CPU block.
void on_ipi();
void on_scheduler_ipi();

}  // namespace cxc::smp
