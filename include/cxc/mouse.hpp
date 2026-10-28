#pragma once

#include <stdint.h>

namespace cxc::mouse {

struct State {
    int32_t x;
    int32_t y;
    uint8_t buttons;  // bit0=left bit1=right bit2=middle
    uint32_t packets;
};

// Enable AUX device on i8042 (IRQ12). Safe if mouse absent (synthetic still works).
bool initialize();
bool ready();

void poll_hw();  // drain controller (also called from IRQ)
State state();

// Contract: inject a 3-byte PS/2 relative packet (after enable byte consumed).
void inject_packet(uint8_t b0, uint8_t b1, uint8_t b2);

void set_bounds(int32_t max_x, int32_t max_y);

}  // namespace cxc::mouse
