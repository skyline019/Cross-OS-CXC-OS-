#include <cxc/fbdev.hpp>

#include <cxc/fb.hpp>
#include <cxc/fs.hpp>
#include <cxc/pmm.hpp>
#include <cxc/vmm.hpp>

namespace cxc::fbdev {

bool initialize() {
    return fb::ready();
}

bool is_fb_fd_backend(uint8_t backend_id) {
    return backend_id == fs::backend_fb;
}

bool ioctl_get_vscreeninfo(VarScreenInfo& out) {
    if (!fb::ready()) {
        return false;
    }
    out = {};
    out.xres = fb::info().width;
    out.yres = fb::info().height;
    out.xres_virtual = fb::info().pixels_per_scanline;
    out.yres_virtual = fb::info().height;
    out.bits_per_pixel = 32;
    return true;
}

bool map_to_user(uint64_t user_root, uint64_t user_va, uint64_t length,
                 uint32_t& failure) {
    failure = 0;
    if (!fb::ready() || user_root == 0 || (user_va & (pmm::page_size - 1)) != 0 ||
        length == 0) {
        failure = 1;
        return false;
    }
    const uint64_t phys = fb::info().phys_base;
    const uint64_t avail = fb::info().size;
    if (phys == 0 || length > avail) {
        failure = 2;
        return false;
    }
    const uint64_t pages = (length + pmm::page_size - 1) / pmm::page_size;
    for (uint64_t i = 0; i < pages; ++i) {
        // Map FB PA into user VA as writable user pages (WC not available on
        // user map helper — still usable for read/write probes).
        if (!vmm::map_user_page(user_root, user_va + i * pmm::page_size,
                                phys + i * pmm::page_size,
                                vmm::user_map_writable)) {
            failure = 3;
            return false;
        }
    }
    return true;
}

uint64_t fb_phys() {
    return fb::info().phys_base;
}

uint64_t fb_size() {
    return fb::info().size;
}

uint64_t fb_virt() {
    return reinterpret_cast<uint64_t>(fb::virt_base());
}

}  // namespace cxc::fbdev
