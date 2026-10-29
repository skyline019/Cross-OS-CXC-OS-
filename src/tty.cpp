#include <cxc/tty.hpp>

#include <cxc/fs.hpp>
#include <cxc/vt100.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::tty {
namespace {

char ring[ring_cap]{};
size_t head = 0;
size_t tail = 0;
vt100::Screen* screen = nullptr;
bool inited = false;

void push_byte(char ch) {
    const size_t next = (head + 1) % ring_cap;
    if (next == tail) {
        return;
    }
    ring[head] = ch;
    head = next;
}

}  // namespace

bool initialize() {
    head = 0;
    tail = 0;
    screen = nullptr;
    inited = true;
    return true;
}

void attach_screen(void* vt_screen) {
    screen = static_cast<vt100::Screen*>(vt_screen);
}

void* attached_screen() {
    return screen;
}

size_t write(const char* data, size_t len) {
    if (!inited || data == nullptr) {
        return 0;
    }
    if (screen != nullptr) {
        vt100::write(*screen, data, len);
    }
    for (size_t i = 0; i < len; ++i) {
        push_byte(data[i]);
    }
    return len;
}

size_t read(char* data, size_t cap) {
    if (!inited || data == nullptr || cap == 0) {
        return 0;
    }
    size_t n = 0;
    while (n < cap && tail != head) {
        data[n++] = ring[tail];
        tail = (tail + 1) % ring_cap;
    }
    return n;
}

bool is_tty_fd_backend(uint8_t backend_id) {
    return backend_id == fs::backend_tty;
}

bool ioctl_tcgets(void* buf, size_t len) {
    // Minimal linux termios (36+ bytes varies); zero-fill and mark as tty.
    if (buf == nullptr || len < 36) {
        return false;
    }
    auto* bytes = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) {
        bytes[i] = 0;
    }
    // c_cflag: CS8|CREAD roughly
    bytes[8] = 0x30;
    bytes[9] = 0x01;
    return true;
}

}  // namespace cxc::tty
