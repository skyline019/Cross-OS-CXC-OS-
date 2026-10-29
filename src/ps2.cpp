#include <cxc/ps2.hpp>

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/irq.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::ps2 {
namespace {

constexpr uint16_t data_port = 0x60;
constexpr uint16_t status_port = 0x64;
constexpr size_t ring_cap = 64;

Event ring[ring_cap];
volatile size_t head = 0;
volatile size_t tail = 0;
volatile uint64_t overflows = 0;
volatile uint64_t irqs = 0;
bool inited = false;
bool e0_prefix = false;
bool shift = false;

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void push_event(Event ev) {
    const size_t next = (head + 1) % ring_cap;
    if (next == tail) {
        __atomic_add_fetch(&overflows, 1, __ATOMIC_RELAXED);
        return;
    }
    ring[head] = ev;
    __atomic_store_n(&head, next, __ATOMIC_RELEASE);
}

char translate_set1(uint8_t code, bool shifted) {
    static const char lower[58] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' ',
    };
    static const char upper[58] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0, 0, ' ',
    };
    if (code >= sizeof(lower)) {
        return 0;
    }
    return shifted ? upper[code] : lower[code];
}

void handle_scancode(uint8_t code) {
    if (code == 0xe0) {
        e0_prefix = true;
        return;
    }
    const bool release = (code & 0x80) != 0;
    const uint8_t make = code & 0x7f;
    if (e0_prefix) {
        e0_prefix = false;
        if (release) {
            return;
        }
        Event ev{Key::None, 0};
        if (make == 0x4b) {
            ev.key = Key::Left;
        } else if (make == 0x4d) {
            ev.key = Key::Right;
        } else if (make == 0x48) {
            ev.key = Key::Up;
        } else if (make == 0x50) {
            ev.key = Key::Down;
        } else if (make == 0x49) {
            ev.key = Key::PageUp;
        } else if (make == 0x51) {
            ev.key = Key::PageDown;
        } else {
            return;
        }
        push_event(ev);
        return;
    }
    if (make == 0x2a || make == 0x36) {
        shift = !release;
        return;
    }
    if (release) {
        return;
    }
    Event ev{Key::None, 0};
    if (make == 0x1c) {
        ev.key = Key::Enter;
    } else if (make == 0x0e) {
        ev.key = Key::Backspace;
    } else if (make == 0x01) {
        ev.key = Key::Escape;
    } else if (make == 0x0f) {
        ev.key = Key::Tab;
    } else if (make == 0x3c) {
        ev.key = Key::F2;
    } else if (make == 0x3d) {
        ev.key = Key::F3;
    } else if (make == 0x3e) {
        ev.key = Key::F4;
    } else if (make == 0x3f) {
        ev.key = Key::F5;
    } else if (make == 0x40) {
        ev.key = Key::F6;
    } else if (make == 0x41) {
        ev.key = Key::F7;
    } else if (make == 0x43) {
        ev.key = Key::F9;
    } else {
        const char ch = translate_set1(make, shift);
        if (ch == 0) {
            return;
        }
        ev.key = Key::Char;
        ev.ch = ch;
    }
    push_event(ev);
}

void irq_handler(void*) {
    __atomic_add_fetch(&irqs, 1, __ATOMIC_RELAXED);
    while ((inb(status_port) & 1) != 0) {
        handle_scancode(inb(data_port));
    }
}

}  // namespace

bool initialize() {
    inited = false;
    head = 0;
    tail = 0;
    overflows = 0;
    irqs = 0;
    e0_prefix = false;
    shift = false;
    if (cpu::interrupts_enabled()) {
        return false;
    }
    // Drain output buffer.
    for (uint32_t i = 0; i < 256; ++i) {
        if ((inb(status_port) & 1) == 0) {
            break;
        }
        (void)inb(data_port);
    }
    // Enable keyboard scanning via controller.
    outb(status_port, 0xae);
    uint8_t vector = 0;
    if (!irq::register_isa(1, irq_handler, nullptr, vector)) {
        // Still allow synthetic inject path.
        inited = true;
        return true;
    }
    (void)vector;
    inited = true;
    return true;
}

bool ready() {
    return inited;
}

bool poll(Event& out) {
    const size_t t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
    const size_t h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
    if (t == h) {
        return false;
    }
    out = ring[t];
    __atomic_store_n(&tail, (t + 1) % ring_cap, __ATOMIC_RELEASE);
    return true;
}

void inject_char(char ch) {
    Event ev{Key::Char, ch};
    push_event(ev);
}

void inject_key(Key key) {
    Event ev{key, 0};
    push_event(ev);
}

uint64_t overflow_count() {
    return __atomic_load_n(&overflows, __ATOMIC_RELAXED);
}

uint64_t irq_count() {
    return __atomic_load_n(&irqs, __ATOMIC_RELAXED);
}

}  // namespace cxc::ps2
