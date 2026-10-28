#include <cxc/wm.hpp>

#include <cxc/fb.hpp>

namespace cxc::wm {
namespace {

Window wins[max_windows]{};
uint32_t focus = 0;
uint32_t count = 0;
uint32_t scale_v = 1;
bool left_was_down = false;
bool dragging = false;
uint32_t drag_index = 0;
int32_t drag_ox = 0;
int32_t drag_oy = 0;

constexpr uint32_t col_desktop = 0xff0b121a;
constexpr uint32_t col_chrome = 0xff2a3b4d;
constexpr uint32_t col_focus = 0xff3d5a80;
constexpr uint32_t col_body = 0xff15202b;
constexpr uint32_t col_text = 0xffe8eef4;
constexpr uint32_t col_pointer = 0xffffcc66;

void clamp_window(Window& w) {
    if (!fb::ready()) {
        return;
    }
    const int32_t W = static_cast<int32_t>(fb::info().width);
    const int32_t H = static_cast<int32_t>(fb::info().height);
    if (w.w < 80) {
        w.w = 80;
    }
    if (w.h < 48) {
        w.h = 48;
    }
    if (w.x + 40 > W) {
        w.x = W - 40;
    }
    if (w.y + static_cast<int32_t>(title_h) > H) {
        w.y = H - static_cast<int32_t>(title_h);
    }
    if (w.x < -static_cast<int32_t>(w.w) + 40) {
        w.x = -static_cast<int32_t>(w.w) + 40;
    }
    if (w.y < 0) {
        w.y = 0;
    }
}

}  // namespace

bool initialize() {
    count = 0;
    focus = 0;
    scale_v = 1;
    left_was_down = false;
    dragging = false;
    drag_index = 0;
    for (uint32_t i = 0; i < max_windows; ++i) {
        wins[i] = {};
    }
    return fb::ready();
}

void set_font_scale(uint32_t scale) {
    scale_v = (scale >= 2) ? 2u : 1u;
    fb::set_scale(scale_v);
}

uint32_t font_scale() {
    return scale_v;
}

void layout_default() {
    if (!fb::ready()) {
        return;
    }
    const uint32_t W = fb::info().width;
    const uint32_t H = fb::info().height;
    wins[0] = {20, 40, W / 2 - 40, H - 80, Kind::Ide, true, "IDE"};
    wins[1] = {static_cast<int32_t>(W / 2 + 10), 40, W / 2 - 40, H - 80,
               Kind::Term, true, "Terminal"};
    count = 2;
    focus = 0;
    dragging = false;
}

void layout_ide() {
    if (!fb::ready()) {
        return;
    }
    const uint32_t W = fb::info().width;
    const uint32_t H = fb::info().height;
    const uint32_t top = 28;
    const uint32_t tree_w = W / 5;
    if (tree_w < 120) {
        // Fall back to stacked panes on tiny FB.
        wins[0] = {4, static_cast<int32_t>(top), W - 8, H / 3,
                   Kind::Tree, true, "Files"};
        wins[1] = {4, static_cast<int32_t>(top + H / 3), W - 8, H / 3,
                   Kind::Editor, true, "Editor"};
        wins[2] = {4, static_cast<int32_t>(top + 2 * (H / 3)), W - 8,
                   H - top - 2 * (H / 3) - 4, Kind::Term, true, "Terminal"};
        count = 3;
        focus = 1;
        dragging = false;
        return;
    }
    const uint32_t term_h = H / 3;
    const uint32_t editor_h = H - top - term_h - 8;
    wins[0] = {4, static_cast<int32_t>(top), tree_w - 8, H - top - 8,
               Kind::Tree, true, "Files"};
    wins[1] = {static_cast<int32_t>(tree_w), static_cast<int32_t>(top),
               W - tree_w - 8, editor_h, Kind::Editor, true, "Editor"};
    wins[2] = {static_cast<int32_t>(tree_w),
               static_cast<int32_t>(top + editor_h + 4), W - tree_w - 8,
               term_h - 4, Kind::Term, true, "Terminal"};
    count = 3;
    focus = 1;
    dragging = false;
}

void draw_chrome() {
    if (!fb::ready()) {
        return;
    }
    fb::clear(col_desktop);
    fb::puts(1, 0, "CXC/OS IDE  F2Save F3Close F4Open F5Build F6Tab F7Tree F9Run Esc",
             col_text, col_desktop);
    for (uint32_t i = 0; i < count; ++i) {
        Window& w = wins[i];
        if (!w.visible) {
            continue;
        }
        clamp_window(w);
        const uint32_t chrome = (i == focus) ? col_focus : col_chrome;
        fb::fill_rect(static_cast<uint32_t>(w.x < 0 ? 0 : w.x),
                      static_cast<uint32_t>(w.y < 0 ? 0 : w.y), w.w, title_h,
                      chrome);
        if (w.h > title_h) {
            fb::fill_rect(static_cast<uint32_t>(w.x < 0 ? 0 : w.x),
                          static_cast<uint32_t>(w.y) + title_h, w.w,
                          w.h - title_h, col_body);
        }
        if (w.x >= 0 && w.y >= 0) {
            const uint32_t title_col =
                static_cast<uint32_t>(w.x) / fb::glyph_w() + 1;
            const uint32_t title_row =
                static_cast<uint32_t>(w.y) / fb::glyph_h();
            fb::puts(title_col, title_row, w.title != nullptr ? w.title : "win",
                     col_text, chrome);
        }
    }
}

void draw_pointer(int32_t x, int32_t y) {
    if (x < 0 || y < 0) {
        return;
    }
    fb::fill_rect(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 8, 8,
                  col_pointer);
}

bool hit_test(int32_t x, int32_t y, uint32_t& out_index, bool& titlebar) {
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        Window& w = wins[static_cast<uint32_t>(i)];
        if (!w.visible) {
            continue;
        }
        if (x >= w.x && x < w.x + static_cast<int32_t>(w.w) && y >= w.y &&
            y < w.y + static_cast<int32_t>(w.h)) {
            out_index = static_cast<uint32_t>(i);
            titlebar = (y < w.y + static_cast<int32_t>(title_h));
            return true;
        }
    }
    return false;
}

bool on_pointer(int32_t x, int32_t y, uint8_t buttons) {
    const bool left = (buttons & 1) != 0;
    bool need_chrome = false;
    if (dragging) {
        if (!left) {
            dragging = false;
            need_chrome = true;
        } else {
            Window& w = wins[drag_index];
            w.x = x - drag_ox;
            w.y = y - drag_oy;
            clamp_window(w);
            need_chrome = true;
        }
        left_was_down = left;
        return need_chrome;
    }
    if (left && !left_was_down) {
        uint32_t hit = 0;
        bool tb = false;
        if (hit_test(x, y, hit, tb)) {
            if (hit != focus) {
                raise(hit);
                need_chrome = true;
            }
            if (tb) {
                dragging = true;
                drag_index = hit;
                drag_ox = x - wins[hit].x;
                drag_oy = y - wins[hit].y;
            }
        }
    }
    left_was_down = left;
    return need_chrome;
}

bool client_of(uint32_t index, Client& out) {
    if (index >= count || !wins[index].visible) {
        return false;
    }
    Window& w = wins[index];
    if (w.h <= title_h || w.w == 0) {
        return false;
    }
    const int32_t cx = w.x;
    const int32_t cy = w.y + static_cast<int32_t>(title_h);
    const uint32_t cw = w.w;
    const uint32_t ch = w.h - title_h;
    if (cx < 0 || cy < 0) {
        out.px = 0;
        out.py = 0;
        out.pw = 0;
        out.ph = 0;
        out.col0 = 0;
        out.row0 = 0;
        out.cols = 0;
        out.rows = 0;
        return false;
    }
    out.px = static_cast<uint32_t>(cx);
    out.py = static_cast<uint32_t>(cy);
    out.pw = cw;
    out.ph = ch;
    const uint32_t gw = fb::glyph_w();
    const uint32_t gh = fb::glyph_h();
    if (gw == 0 || gh == 0) {
        return false;
    }
    out.col0 = out.px / gw;
    out.row0 = out.py / gh;
    out.cols = cw / gw;
    out.rows = ch / gh;
    return out.cols > 0 && out.rows > 0;
}

int32_t find_kind(Kind kind) {
    for (uint32_t i = 0; i < count; ++i) {
        if (wins[i].visible && wins[i].kind == kind) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void set_focus(uint32_t index) {
    if (index < count) {
        focus = index;
    }
}

void raise(uint32_t index) {
    if (index >= count || index + 1 == count) {
        if (index < count) {
            focus = index;
        }
        return;
    }
    const Window tmp = wins[index];
    for (uint32_t i = index; i + 1 < count; ++i) {
        wins[i] = wins[i + 1];
    }
    wins[count - 1] = tmp;
    focus = count - 1;
}

uint32_t focused() {
    return focus;
}

Window* window(uint32_t index) {
    return index < count ? &wins[index] : nullptr;
}

uint32_t window_count() {
    return count;
}

}  // namespace cxc::wm
