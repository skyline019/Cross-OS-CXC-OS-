#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::tty {

constexpr size_t ring_cap = 4096;

bool initialize();
// Kernel console tied to VT100 screen + optional FB window.
void attach_screen(void* vt_screen);  // cxc::vt100::Screen*
void* attached_screen();

size_t write(const char* data, size_t len);
size_t read(char* data, size_t cap);
bool is_tty_fd_backend(uint8_t backend_id);

// Linux TCGETS (0x5401) success → fills a minimal termios-shaped buffer.
bool ioctl_tcgets(void* buf, size_t len);

}  // namespace cxc::tty
