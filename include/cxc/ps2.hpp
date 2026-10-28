#pragma once

#include <stdint.h>

namespace cxc::ps2 {

enum class Key : uint32_t {
    None = 0,
    Char = 1,
    Enter = 2,
    Backspace = 3,
    Left = 4,
    Right = 5,
    Up = 6,
    Down = 7,
    F2 = 8,
    F5 = 9,
    F9 = 10,
    Escape = 11,
    PageUp = 12,
    PageDown = 13,
    Tab = 14,
    F3 = 15,
    F4 = 16,
    F6 = 17,
    F7 = 18,
};

struct Event {
    Key key;
    char ch;  // valid when key == Char
};

// Register IRQ1. Requires IF clear during registration; enables after.
bool initialize();
bool ready();

// Non-blocking pop; returns false if empty.
bool poll(Event& out);

// Contract / headless: push synthetic events (no hardware needed).
void inject_char(char ch);
void inject_key(Key key);

uint64_t overflow_count();
uint64_t irq_count();

}  // namespace cxc::ps2
