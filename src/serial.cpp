#include <cxc/serial.hpp>

#include <cxc/acpi.hpp>
#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/irq.hpp>
#include <cxc/scheduler.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::serial {
namespace {

constexpr uint16_t com1 = 0x3f8;
constexpr uint8_t expected_bytes[4] = {0x41, 0x5a, 0xa5, 0x3c};
bool available = false;
volatile uint64_t loopback_calls = 0;
volatile uint64_t loopback_count = 0;
volatile uint64_t loopback_errors = 0;
uint8_t loopback_received[4];

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void raw_write(char value) {
    // Avoid an infinite loop on hardware without a responding UART.
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        if ((inb(com1 + 5) & 0x20) != 0) {
            outb(com1, static_cast<uint8_t>(value));
            return;
        }
        __asm__ volatile("pause");
    }
}

void loopback_handler(void*) {
    __atomic_add_fetch(&loopback_calls, 1, __ATOMIC_RELAXED);
    const uint8_t identification = inb(com1 + 2);
    if ((identification & 1) != 0) {
        __atomic_add_fetch(&loopback_errors, 1, __ATOMIC_RELAXED);
        return;
    }
    const uint8_t reason = identification & 0x0f;
    if (reason != 0x04 && reason != 0x0c) {
        __atomic_add_fetch(&loopback_errors, 1, __ATOMIC_RELAXED);
        return;
    }

    while ((inb(com1 + 5) & 1) != 0) {
        const uint8_t value = inb(com1);
        const uint64_t index = __atomic_load_n(&loopback_count, __ATOMIC_RELAXED);
        if (index >= 4 || value != expected_bytes[index]) {
            __atomic_add_fetch(&loopback_errors, 1, __ATOMIC_RELAXED);
        } else {
            loopback_received[index] = value;
        }
        __atomic_add_fetch(&loopback_count, 1, __ATOMIC_RELEASE);
    }
}

void restore_polling_mode() {
    outb(com1 + 1, 0x00);
    outb(com1 + 4, 0x0b);
    outb(com1 + 2, 0xc7);
}

}  // namespace

void initialize() {
    outb(com1 + 1, 0x00);  // Disable UART interrupts.
    outb(com1 + 3, 0x80);  // Enable divisor latch.
    outb(com1 + 0, 0x01);  // 115200 baud.
    outb(com1 + 1, 0x00);
    outb(com1 + 3, 0x03);  // 8 data bits, no parity, one stop bit.
    outb(com1 + 2, 0xc7);  // Enable and clear FIFOs.
    outb(com1 + 4, 0x0b);  // DTR, RTS, and auxiliary output 2.

    const uint8_t line_status = inb(com1 + 5);
    available = line_status != 0xff;
}

bool present() {
    return available;
}

void write(char value) {
    if (!available) {
        return;
    }
    if (value == '\n') {
        raw_write('\r');
    }
    raw_write(value);
}

bool run_irq_loopback_test(uint32_t& gsi, uint8_t& vector) {
    if (!available || cpu::interrupts_enabled()) {
        return false;
    }

    uint16_t flags = 0;
    gsi = acpi::isa_irq_to_gsi(4, flags);
    vector = 0;
    __atomic_store_n(&loopback_calls, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&loopback_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&loopback_errors, 0, __ATOMIC_RELAXED);
    const uint64_t unhandled_before = irq::unhandled_count();

    outb(com1 + 1, 0x00);
    outb(com1 + 4, 0x1b);  // OUT2 remains asserted; enable internal loopback.
    outb(com1 + 2, 0x07);  // Enable/clear FIFOs, one-byte receive trigger.
    (void)inb(com1 + 5);
    while ((inb(com1 + 5) & 1) != 0) {
        (void)inb(com1);
    }

    if (!irq::register_gsi(gsi, flags, loopback_handler, nullptr, vector)) {
        restore_polling_mode();
        return false;
    }
    uint8_t duplicate_vector = 0;
    if (irq::register_gsi(gsi, flags, loopback_handler, nullptr,
                          duplicate_vector)) {
        (void)irq::unregister(duplicate_vector);
        (void)irq::unregister(vector);
        restore_polling_mode();
        return false;  // A second route would falsely imply shared IRQ support.
    }

    outb(com1 + 1, 0x01);  // Receiver data-available interrupt only.
    apic::start_periodic();
    const uint64_t first_tick = scheduler::ticks();
    cpu::enable_interrupts();
    for (size_t index = 0; index < 4; ++index) {
        raw_write(static_cast<char>(expected_bytes[index]));
    }

    while (__atomic_load_n(&loopback_count, __ATOMIC_ACQUIRE) < 4 &&
           __atomic_load_n(&loopback_errors, __ATOMIC_ACQUIRE) == 0) {
        if (scheduler::ticks() - first_tick > 1000) {
            break;
        }
        cpu::wait_for_interrupt();
    }
    cpu::disable_interrupts();
    apic::stop_periodic();
    outb(com1 + 1, 0x00);
    const bool unregistered = irq::unregister(vector);
    restore_polling_mode();

    return unregistered &&
           __atomic_load_n(&loopback_count, __ATOMIC_ACQUIRE) == 4 &&
           __atomic_load_n(&loopback_errors, __ATOMIC_ACQUIRE) == 0 &&
           __atomic_load_n(&loopback_calls, __ATOMIC_ACQUIRE) != 0 &&
           irq::unhandled_count() == unhandled_before;
}

uint64_t irq_loopback_calls() {
    return __atomic_load_n(&loopback_calls, __ATOMIC_ACQUIRE);
}

}  // namespace cxc::serial
