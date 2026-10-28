#include <cxc/mouse.hpp>

#include <cxc/cpu.hpp>
#include <cxc/irq.hpp>

#include <stdint.h>

namespace cxc::mouse {
namespace {

constexpr uint16_t data_port = 0x60;
constexpr uint16_t status_port = 0x64;

State cur{};
bool inited = false;
uint8_t pkt[3]{};
uint32_t pkt_i = 0;
int32_t max_x = 1279;
int32_t max_y = 799;

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void wait_write() {
    for (uint32_t i = 0; i < 100000; ++i) {
        if ((inb(status_port) & 2) == 0) {
            return;
        }
    }
}

void wait_read() {
    for (uint32_t i = 0; i < 100000; ++i) {
        if ((inb(status_port) & 1) != 0) {
            return;
        }
    }
}

void write_cmd(uint8_t cmd) {
    wait_write();
    outb(status_port, cmd);
}

void write_data(uint8_t data) {
    wait_write();
    outb(data_port, data);
}

void write_mouse(uint8_t data) {
    write_cmd(0xd4);
    write_data(data);
    wait_read();
    (void)inb(data_port);  // ACK
}

void apply_packet(uint8_t b0, uint8_t b1, uint8_t b2) {
    if ((b0 & 0x08) == 0) {
        return;  // bit3 must be set in first byte
    }
    int32_t dx = static_cast<int32_t>(b1);
    int32_t dy = static_cast<int32_t>(b2);
    if ((b0 & 0x10) != 0) {
        dx |= static_cast<int32_t>(0xffffff00u);
    }
    if ((b0 & 0x20) != 0) {
        dy |= static_cast<int32_t>(0xffffff00u);
    }
    cur.x += dx;
    cur.y -= dy;  // PS/2 Y is opposite to FB
    if (cur.x < 0) {
        cur.x = 0;
    }
    if (cur.y < 0) {
        cur.y = 0;
    }
    if (cur.x > max_x) {
        cur.x = max_x;
    }
    if (cur.y > max_y) {
        cur.y = max_y;
    }
    cur.buttons = static_cast<uint8_t>(b0 & 0x07);
    ++cur.packets;
}

void on_byte(uint8_t value) {
    if (pkt_i == 0 && (value & 0x08) == 0) {
        return;
    }
    pkt[pkt_i++] = value;
    if (pkt_i >= 3) {
        apply_packet(pkt[0], pkt[1], pkt[2]);
        pkt_i = 0;
    }
}

void irq_handler(void*) {
    while ((inb(status_port) & 1) != 0) {
        const uint8_t st = inb(status_port);
        const uint8_t data = inb(data_port);
        if ((st & 0x20) != 0) {
            on_byte(data);
        }
    }
}

}  // namespace

bool initialize() {
    inited = false;
    cur = {};
    pkt_i = 0;
    if (cpu::interrupts_enabled()) {
        return false;
    }
    // Enable AUX clock + IRQ.
    write_cmd(0xa8);
    write_cmd(0x20);
    wait_read();
    uint8_t status = inb(data_port);
    status = static_cast<uint8_t>((status | 0x02) & ~0x20u);
    write_cmd(0x60);
    write_data(status);
    write_mouse(0xf6);  // defaults
    write_mouse(0xf4);  // enable streaming
    uint8_t vector = 0;
    (void)irq::register_isa(12, irq_handler, nullptr, vector);
    inited = true;
    return true;
}

bool ready() {
    return inited;
}

void poll_hw() {
    irq_handler(nullptr);
}

State state() {
    return cur;
}

void inject_packet(uint8_t b0, uint8_t b1, uint8_t b2) {
    apply_packet(b0, b1, b2);
}

void set_bounds(int32_t bx, int32_t by) {
    if (bx < 0) {
        bx = 0;
    }
    if (by < 0) {
        by = 0;
    }
    max_x = bx;
    max_y = by;
    if (cur.x > max_x) {
        cur.x = max_x;
    }
    if (cur.y > max_y) {
        cur.y = max_y;
    }
}

}  // namespace cxc::mouse
