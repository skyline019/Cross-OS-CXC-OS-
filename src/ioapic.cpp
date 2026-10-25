#include <cxc/ioapic.hpp>

#include <cxc/acpi.hpp>
#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::ioapic {
namespace {

constexpr uint32_t register_select = 0x00;
constexpr uint32_t register_window = 0x10;
constexpr uint32_t register_version = 0x01;
constexpr uint32_t redirection_base = 0x10;
constexpr uint32_t redirection_masked = uint32_t{1} << 16;
constexpr uint32_t redirection_active_low = uint32_t{1} << 13;
constexpr uint32_t redirection_level = uint32_t{1} << 15;
constexpr uint32_t pit_ticks_10ms = 11932;

struct RuntimeIoApic {
    uint64_t virtual_address;
    uint32_t global_irq_base;
    uint32_t entries;
};

RuntimeIoApic controllers[acpi::maximum_ioapics];
uint32_t controller_count = 0;
uint32_t total_entries = 0;
volatile uint64_t irq0_count = 0;
uint32_t irq0_gsi = 0;
uint32_t test_controller = 0;
uint32_t test_pin = 0;
bool test_line_valid = false;
bool ready = false;

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint32_t read_register(uint32_t controller, uint32_t index) {
    const uint64_t base = controllers[controller].virtual_address;
    auto* select = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + register_select));
    const auto* window = reinterpret_cast<volatile const uint32_t*>(
        static_cast<uintptr_t>(base + register_window));
    *select = index;
    return *window;
}

void write_register(uint32_t controller, uint32_t index, uint32_t value) {
    const uint64_t base = controllers[controller].virtual_address;
    auto* select = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + register_select));
    auto* window = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + register_window));
    *select = index;
    *window = value;
}

void write_redirection(uint32_t controller, uint32_t pin,
                       uint32_t low, uint32_t high) {
    const uint32_t index = redirection_base + pin * 2;
    write_register(controller, index + 1, high);
    write_register(controller, index, low);
}

bool locate_gsi(uint32_t gsi, uint32_t& controller, uint32_t& pin) {
    for (uint32_t index = 0; index < controller_count; ++index) {
        const uint32_t base = controllers[index].global_irq_base;
        if (gsi >= base && gsi - base < controllers[index].entries) {
            controller = index;
            pin = gsi - base;
            return true;
        }
    }
    return false;
}

void mask_test_line() {
    if (!test_line_valid) {
        return;
    }
    const uint32_t index = redirection_base + test_pin * 2;
    const uint32_t low = read_register(test_controller, index);
    write_register(test_controller, index, low | redirection_masked);
}

uint32_t flags_to_rte(uint16_t flags, uint8_t vector) {
    uint32_t low = vector;
    const uint16_t polarity = flags & 3;
    const uint16_t trigger = (flags >> 2) & 3;
    if (polarity == 3) {
        low |= redirection_active_low;
    }
    if (trigger == 3) {
        low |= redirection_level;
    }
    // Conforming ISA defaults and explicit edge/high require no extra bits.
    return low;
}

}  // namespace

bool initialize() {
    if (ready) {
        return true;
    }
    if (!apic::initialized() || !acpi::discovered() ||
        cpu::interrupts_enabled() || apic::apic_id() > 0xff) {
        return false;
    }

    const acpi::MadtInfo& madt = acpi::madt();
    if (madt.ioapic_count == 0 || madt.ioapic_count > acpi::maximum_ioapics) {
        return false;
    }

    for (uint32_t index = 0; index < madt.ioapic_count; ++index) {
        const acpi::IoApicDescriptor& descriptor = madt.ioapics[index];
        const uint64_t virtual_address = virtual_base + index * virtual_stride;
        if (descriptor.address == 0 ||
            (descriptor.address & 0xfff) != 0 ||
            vmm::is_mapped(virtual_address) ||
            !vmm::map_device_page(descriptor.address, virtual_address)) {
            return false;
        }
        controllers[index].virtual_address = virtual_address;
        controllers[index].global_irq_base = descriptor.global_irq_base;
        controllers[index].entries =
            ((read_register(index, register_version) >> 16) & 0xff) + 1;
        if (controllers[index].entries == 0 || controllers[index].entries > 120) {
            return false;
        }

        for (uint32_t pin = 0; pin < controllers[index].entries; ++pin) {
            const uint32_t reg = redirection_base + pin * 2;
            const uint32_t low = read_register(index, reg);
            write_register(index, reg, low | redirection_masked);
        }
        ++controller_count;
        total_entries += controllers[index].entries;
    }

    ready = true;
    return true;
}

bool route_gsi(uint32_t gsi, uint16_t flags, uint8_t vector) {
    if (!ready) {
        return false;
    }
    uint32_t controller = 0;
    uint32_t pin = 0;
    if (!locate_gsi(gsi, controller, pin)) {
        return false;
    }
    const uint32_t low = flags_to_rte(flags, vector);
    const uint32_t high = apic::apic_id() << 24;
    write_redirection(controller, pin, low | redirection_masked, high);
    write_redirection(controller, pin, low, high);
    return true;
}

bool mask_gsi(uint32_t gsi) {
    if (!ready) {
        return false;
    }
    uint32_t controller = 0;
    uint32_t pin = 0;
    if (!locate_gsi(gsi, controller, pin)) {
        return false;
    }
    const uint32_t reg = redirection_base + pin * 2;
    const uint32_t low = read_register(controller, reg);
    write_register(controller, reg, low | redirection_masked);
    return true;
}

bool run_irq0_test() {
    if (!ready || cpu::interrupts_enabled()) {
        return false;
    }

    uint16_t flags = 0;
    irq0_gsi = acpi::isa_irq_to_gsi(0, flags);
    if (!locate_gsi(irq0_gsi, test_controller, test_pin)) {
        return false;
    }

    const uint32_t low = flags_to_rte(flags, cpu::ioapic_test_vector);
    const uint32_t high = apic::apic_id() << 24;
    write_redirection(test_controller, test_pin,
                      low | redirection_masked, high);
    test_line_valid = true;
    __atomic_store_n(&irq0_count, 0, __ATOMIC_RELAXED);

    // The local timer supplies a bounded wait even if the IOAPIC route is bad.
    apic::start_periodic();
    outb(0x43, 0x30);  // PIT channel 0, low/high, mode 0 one-shot.
    outb(0x40, static_cast<uint8_t>(pit_ticks_10ms & 0xff));
    outb(0x40, static_cast<uint8_t>(pit_ticks_10ms >> 8));
    write_redirection(test_controller, test_pin, low, high);

    const uint64_t first_tick = scheduler::ticks();
    cpu::enable_interrupts();
    while (__atomic_load_n(&irq0_count, __ATOMIC_ACQUIRE) == 0) {
        if (scheduler::ticks() - first_tick > 1000) {
            break;
        }
        cpu::wait_for_interrupt();
    }
    cpu::disable_interrupts();
    apic::stop_periodic();
    mask_test_line();

    return __atomic_load_n(&irq0_count, __ATOMIC_ACQUIRE) == 1;
}

void on_interrupt() {
    // Masking in the handler also makes a level-triggered override safe: the
    // PIT output may remain asserted after its one-shot has expired.
    mask_test_line();
    __atomic_add_fetch(&irq0_count, 1, __ATOMIC_RELEASE);
    apic::end_of_interrupt();
}

uint32_t redirection_entries() {
    return total_entries;
}

uint32_t tested_gsi() {
    return irq0_gsi;
}

uint64_t test_interrupts() {
    return __atomic_load_n(&irq0_count, __ATOMIC_ACQUIRE);
}

bool initialized() {
    return ready;
}

}  // namespace cxc::ioapic
