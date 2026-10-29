#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::fbdev {

// Linux FBIOGET_VSCREENINFO
constexpr uint64_t fbioget_vscreeninfo = 0x4600;

struct VarScreenInfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
    uint32_t rotate;
    uint32_t reserved[8];
};

bool initialize();
bool is_fb_fd_backend(uint8_t backend_id);
bool ioctl_get_vscreeninfo(VarScreenInfo& out);

// Map framebuffer into the active user address space at `user_va` (page-aligned).
bool map_to_user(uint64_t user_root, uint64_t user_va, uint64_t length,
                 uint32_t& failure);

uint64_t fb_phys();
uint64_t fb_size();
uint64_t fb_virt();

}  // namespace cxc::fbdev
