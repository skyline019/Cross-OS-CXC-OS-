#include <cxc/guiux.hpp>

#include <cxc/cpu.hpp>
#include <cxc/fb.hpp>
#include <cxc/fbdev.hpp>
#include <cxc/fs.hpp>
#include <cxc/log.hpp>
#include <cxc/mouse.hpp>
#include <cxc/tty.hpp>
#include <cxc/vt100.hpp>
#include <cxc/wm.hpp>

#include <stddef.h>

namespace cxc::guiux {
namespace {

// Keep off the stack: Screen is ~4.8 KiB and UEFI MinGW lacks ___chkstk_ms.
vt100::Screen contract_screen{};

size_t cstrlen(const char* s) {
    size_t n = 0;
    if (s == nullptr) {
        return 0;
    }
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

}  // namespace

bool run_automated_contract(ContractResult& result) {
    result = {};
    result.stage = 1;

    if (!fb::ready() && !fb::initialize()) {
        result.failure = 1;
        return false;
    }
    if (fb::info().width > 0 && fb::info().height > 0) {
        mouse::set_bounds(static_cast<int32_t>(fb::info().width - 1),
                          static_cast<int32_t>(fb::info().height - 1));
    }

    cpu::disable_interrupts();
    (void)mouse::initialize();
    cpu::enable_interrupts();
    mouse::inject_packet(0x09, 12, 0);  // bit3|left, dx=12
    const mouse::State ms = mouse::state();
    if (ms.packets < 1 || ms.x < 12) {
        result.failure = 10;
        return false;
    }
    result.mouse = 1;
    result.stage = 2;

    if (!wm::initialize()) {
        result.failure = 20;
        return false;
    }
    wm::set_font_scale(2);
    if (wm::font_scale() != 2 || fb::scale() != 2 || fb::glyph_w() != 16) {
        result.failure = 21;
        return false;
    }
    wm::set_font_scale(1);
    wm::layout_default();
    wm::draw_chrome();
    (void)wm::on_pointer(ms.x, ms.y, ms.buttons);
    wm::draw_chrome();
    wm::draw_pointer(ms.x, ms.y);
    if (wm::window_count() < 2 || wm::window(0) == nullptr ||
        wm::window(1) == nullptr) {
        result.failure = 22;
        return false;
    }
    result.windows = 1;
    result.scale = 1;
    result.stage = 3;

    vt100::Screen& scr = contract_screen;
    vt100::reset(scr, 40, 12);
    const char* seq = "\x1b[2J\x1b[H\x1b[32mOK\x1b[0m";
    vt100::write(scr, seq, cstrlen(seq));
    if (scr.cells[0][0] != 'O' || scr.cells[0][1] != 'K') {
        result.failure = 30;
        return false;
    }
    vt100::render(scr, 2, 4);
    result.vt100 = 1;
    result.stage = 4;

    if (!tty::initialize()) {
        result.failure = 40;
        return false;
    }
    tty::attach_screen(&scr);
    if (tty::write("TTY\n", 4) != 4) {
        result.failure = 41;
        return false;
    }
    char ring_buf[8]{};
    if (tty::read(ring_buf, sizeof(ring_buf)) < 3) {
        result.failure = 42;
        return false;
    }
    result.tty = 1;
    result.stage = 5;

    if (!fbdev::initialize()) {
        result.failure = 50;
        return false;
    }
    const int64_t fbfd = fs::open_path("/dev/fb0", fs::o_rdwr, 0);
    const int64_t ttyfd = fs::open_path("/dev/tty0", fs::o_rdwr, 0);
    if (fbfd < 0 || ttyfd < 0) {
        result.failure = 51;
        return false;
    }
    fbdev::VarScreenInfo vsi{};
    if (!fbdev::ioctl_get_vscreeninfo(vsi) || vsi.xres == 0 ||
        vsi.bits_per_pixel != 32) {
        result.failure = 52;
        return false;
    }
    char termios[64]{};
    if (!tty::ioctl_tcgets(termios, sizeof(termios))) {
        result.failure = 53;
        return false;
    }
    if (fs::write_fd(static_cast<uint32_t>(ttyfd), "ux", 2) != 2) {
        result.failure = 54;
        return false;
    }
    fs::StatBuf st{};
    if (fs::fstat_fd(static_cast<uint32_t>(fbfd), st) != 0 ||
        (st.st_mode & fs::s_ifmt) == 0) {
        result.failure = 55;
        return false;
    }
    if (fbdev::fb_phys() == 0 || fbdev::fb_size() == 0) {
        result.failure = 56;
        return false;
    }
    (void)fs::close_fd(static_cast<uint32_t>(fbfd));
    (void)fs::close_fd(static_cast<uint32_t>(ttyfd));
    result.fb0 = 1;
    result.stage = 6;

    fb::puts(2, 10, "ONBOX_GUIUX_PASS", 0xff6bcb77, 0xff0b121a);
    log::line("ONBOX_GUIUX_PASS");
    return true;
}

}  // namespace cxc::guiux
