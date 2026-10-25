#include <cxc/apic.hpp>

#include <cxc/acpi.hpp>
#include <cxc/cpu.hpp>
#include <cxc/log.hpp>
#include <cxc/vmm.hpp>

#include <stdint.h>

namespace cxc::apic {
namespace {

constexpr uint32_t ia32_apic_base = 0x0000001b;
constexpr uint64_t apic_base_address_mask = 0x000ffffffffff000;
constexpr uint64_t apic_x2_enable = uint64_t{1} << 10;
constexpr uint64_t apic_global_enable = uint64_t{1} << 11;

constexpr uint32_t reg_id = 0x20;
constexpr uint32_t reg_version = 0x30;
constexpr uint32_t reg_task_priority = 0x80;
constexpr uint32_t reg_eoi = 0xb0;
constexpr uint32_t reg_spurious = 0xf0;
constexpr uint32_t reg_icr_low = 0x300;
constexpr uint32_t reg_icr_high = 0x310;
constexpr uint32_t reg_lvt_timer = 0x320;
constexpr uint32_t reg_lvt_thermal = 0x330;
constexpr uint32_t reg_lvt_performance = 0x340;
constexpr uint32_t reg_lvt_lint0 = 0x350;
constexpr uint32_t reg_lvt_lint1 = 0x360;
constexpr uint32_t reg_lvt_error = 0x370;
constexpr uint32_t reg_timer_initial = 0x380;
constexpr uint32_t reg_timer_current = 0x390;
constexpr uint32_t reg_timer_divide = 0x3e0;

constexpr uint32_t lvt_masked = uint32_t{1} << 16;
constexpr uint32_t lvt_periodic = uint32_t{1} << 17;
constexpr uint32_t timer_divide_16 = 0x3;
constexpr uint32_t pit_ticks_10ms = 11932;

uint32_t saved_id = 0;
uint32_t calibrated_count = 0;
uint32_t periodic_count = 0;
volatile uint64_t spurious_count = 0;
bool ready = false;
bool x2_mode = false;

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

void cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(0));
}

uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(static_cast<uint32_t>(value)),
                       "d"(static_cast<uint32_t>(value >> 32))
                     : "memory");
}

uint32_t read_register(uint32_t offset) {
    if (x2_mode) {
        return static_cast<uint32_t>(read_msr(0x800 + (offset >> 4)));
    }
    const auto* address = reinterpret_cast<volatile const uint32_t*>(
        static_cast<uintptr_t>(local_apic_virtual_base + offset));
    return *address;
}

void write_register(uint32_t offset, uint32_t value) {
    if (x2_mode) {
        write_msr(0x800 + (offset >> 4), value);
        return;
    }
    auto* address = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(local_apic_virtual_base + offset));
    *address = value;
}

void configure_local_registers() {
    write_register(reg_task_priority, 0);
    write_register(reg_spurious, 0x100 | cpu::spurious_vector);
    write_register(reg_lvt_timer, lvt_masked | cpu::timer_vector);
    write_register(reg_lvt_thermal, lvt_masked);
    write_register(reg_lvt_performance, lvt_masked);
    write_register(reg_lvt_lint0, lvt_masked);
    write_register(reg_lvt_lint1, lvt_masked);
    write_register(reg_lvt_error, lvt_masked);
    write_register(reg_timer_initial, 0);
    write_register(reg_eoi, 0);
}

bool wait_for_icr_delivery() {
    if (x2_mode) {
        return true;  // x2APIC ICR has no Delivery Status bit.
    }
    for (uint32_t guard = 0; guard < 1000000; ++guard) {
        if ((read_register(reg_icr_low) & (uint32_t{1} << 12)) == 0) {
            return true;
        }
        cpu::relax();
    }
    return false;
}

bool send_ipi(uint32_t destination, uint32_t command) {
    if (!ready || (!x2_mode && destination > 0xff) || !wait_for_icr_delivery()) {
        return false;
    }
    if (x2_mode) {
        write_msr(0x830, (static_cast<uint64_t>(destination) << 32) | command);
        return true;
    }
    write_register(reg_icr_high, destination << 24);
    write_register(reg_icr_low, command);
    return wait_for_icr_delivery();
}

void mask_legacy_pic() {
    // The local timer does not need the legacy PIC or IOAPIC. Mask both PICs
    // before IF can be enabled so no unassigned external vector can arrive.
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

bool calibrate_timer() {
    const uint8_t port61 = inb(0x61);
    outb(0x61, (port61 & 0xfc) | 0x01);  // Gate channel 2; speaker stays off.

    // Channel 2, low/high byte, mode 0 one-shot. OUT becomes high at expiry.
    outb(0x43, 0xb0);
    outb(0x42, static_cast<uint8_t>(pit_ticks_10ms & 0xff));
    outb(0x42, static_cast<uint8_t>(pit_ticks_10ms >> 8));

    uint64_t guard = 0;
    while ((inb(0x61) & 0x20) != 0 && guard < 1000000) {
        ++guard;
        cpu::relax();
    }
    if ((inb(0x61) & 0x20) != 0) {
        log::line("[apic] PIT channel 2 did not enter one-shot counting");
        return false;
    }

    write_register(reg_lvt_timer, lvt_masked | cpu::timer_vector);
    write_register(reg_timer_divide, timer_divide_16);
    write_register(reg_timer_initial, 0xffffffffu);

    guard = 0;
    while ((inb(0x61) & 0x20) == 0 && guard < 100000000) {
        ++guard;
        cpu::relax();
    }
    const uint32_t current = read_register(reg_timer_current);
    write_register(reg_timer_initial, 0);
    if ((inb(0x61) & 0x20) == 0) {
        log::line("[apic] PIT channel 2 calibration timed out");
        return false;
    }

    const uint32_t elapsed = 0xffffffffu - current;
    // Calibration measures 10 ms; run the periodic scheduler clock at 1 ms.
    if (elapsed < 10000 || elapsed / 10 < 100) {
        log::line("[apic] implausible local timer calibration");
        return false;
    }
    calibrated_count = elapsed;
    periodic_count = elapsed / 10;
    return true;
}

}  // namespace

bool initialize() {
    if (ready) {
        return true;
    }
    if (cpu::interrupts_enabled()) {
        return false;
    }

    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    cpuid(1, a, b, c, d);
    if ((d & (uint32_t{1} << 9)) == 0) {
        log::line("[apic] CPU does not advertise a local APIC");
        return false;
    }

    uint64_t base_msr = read_msr(ia32_apic_base);
    const uint64_t physical = base_msr & apic_base_address_mask;
    if (physical == 0 || !acpi::discovered() ||
        acpi::madt().local_apic_address != physical) {
        log::line("[apic] MADT and IA32_APIC_BASE addresses disagree");
        return false;
    }

    const bool x2_supported = (c & (uint32_t{1} << 21)) != 0;
    if (x2_supported) {
        base_msr |= apic_global_enable | apic_x2_enable;
        write_msr(ia32_apic_base, base_msr);
        x2_mode = true;
    } else {
        if ((base_msr & apic_x2_enable) != 0) {
            log::line("[apic] x2APIC enabled without CPUID support");
            return false;
        }
        x2_mode = false;
        if ((base_msr & apic_global_enable) == 0) {
            base_msr |= apic_global_enable;
            write_msr(ia32_apic_base, base_msr);
        }
        if (vmm::is_mapped(local_apic_virtual_base) ||
            !vmm::map_device_page(physical, local_apic_virtual_base)) {
            log::line("[apic] cannot map local APIC MMIO page");
            return false;
        }
    }

    const uint32_t version = read_register(reg_version) & 0xff;
    if (version == 0 || version >= 0x80) {
        log::line("[apic] invalid local APIC version");
        return false;
    }

    const uint32_t raw_id = read_register(reg_id);
    saved_id = x2_mode ? raw_id : (raw_id >> 24);
    configure_local_registers();
    mask_legacy_pic();

    if (!calibrate_timer()) {
        write_register(reg_spurious, cpu::spurious_vector);
        return false;
    }

    ready = true;
    return true;
}

bool initialize_application_processor(uint32_t expected_apic_id) {
    if (!ready || cpu::interrupts_enabled() || !acpi::discovered()) {
        return false;
    }
    uint64_t base_msr = read_msr(ia32_apic_base);
    const uint64_t physical = base_msr & apic_base_address_mask;
    if (physical == 0 || acpi::madt().local_apic_address != physical) {
        return false;
    }
    if (x2_mode) {
        base_msr |= apic_global_enable | apic_x2_enable;
        write_msr(ia32_apic_base, base_msr);
    } else {
        if ((base_msr & apic_x2_enable) != 0 ||
            !vmm::is_mapped(local_apic_virtual_base)) {
            return false;
        }
        base_msr |= apic_global_enable;
        write_msr(ia32_apic_base, base_msr);
    }

    const uint32_t raw_id = read_register(reg_id);
    const uint32_t current_id = x2_mode ? raw_id : (raw_id >> 24);
    if (current_id != expected_apic_id) {
        return false;
    }
    configure_local_registers();
    return true;
}

void start_periodic() {
    if (!ready || periodic_count == 0) {
        return;
    }
    write_register(reg_timer_initial, 0);
    write_register(reg_timer_divide, timer_divide_16);
    write_register(reg_lvt_timer, lvt_periodic | cpu::timer_vector);
    write_register(reg_timer_initial, periodic_count);
}

void stop_periodic() {
    if (!ready) {
        return;
    }
    write_register(reg_lvt_timer, lvt_masked | cpu::timer_vector);
    write_register(reg_timer_initial, 0);
}

void end_of_interrupt() {
    if (ready) {
        write_register(reg_eoi, 0);
    }
}

void note_spurious() {
    __atomic_add_fetch(&spurious_count, 1, __ATOMIC_RELAXED);
}

uint32_t apic_id() {
    return saved_id;
}

uint32_t current_apic_id() {
    const uint32_t raw_id = read_register(reg_id);
    return x2_mode ? raw_id : (raw_id >> 24);
}

bool send_init_ipi(uint32_t destination_apic_id) {
    // Physical destination, INIT, level-triggered and asserted.
    return send_ipi(destination_apic_id, 0x0000c500);
}

bool send_startup_ipi(uint32_t destination_apic_id, uint8_t page) {
    return send_ipi(destination_apic_id, 0x00000600 | page);
}

bool send_fixed_ipi(uint32_t destination_apic_id, uint8_t vector) {
    if (vector < 16) {
        return false;
    }
    return send_ipi(destination_apic_id, vector);
}

uint32_t calibration_ticks() {
    return calibrated_count;
}

uint32_t periodic_ticks() {
    return periodic_count;
}

uint64_t spurious_interrupts() {
    return __atomic_load_n(&spurious_count, __ATOMIC_RELAXED);
}

bool initialized() {
    return ready;
}

bool uses_x2apic() {
    return ready && x2_mode;
}

const char* mode_name() {
    return x2_mode ? "x2APIC" : "xAPIC";
}

}  // namespace cxc::apic
