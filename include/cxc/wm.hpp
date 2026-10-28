#pragma once

#include <stdint.h>

namespace cxc::wm {

enum class Kind : uint32_t { Ide = 0, Term = 1, Tree = 2, Editor = 3 };

struct Window {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    Kind kind;
    bool visible;
    const char* title;
};

struct Client {
    uint32_t px;  // client origin pixels
    uint32_t py;
    uint32_t pw;
    uint32_t ph;
    uint32_t col0;  // glyph grid covering client
    uint32_t row0;
    uint32_t cols;
    uint32_t rows;
};

constexpr uint32_t max_windows = 4;
constexpr uint32_t title_h = 24;

bool initialize();
void set_font_scale(uint32_t scale);  // 1 or 2
uint32_t font_scale();

// GUIUX contract: Ide + Term side-by-side.
void layout_default();
// Mini-IDE desktop: Tree | Editor over Term.
void layout_ide();

// Chrome only (no client content). Clears desktop background.
void draw_chrome();
void draw_pointer(int32_t x, int32_t y);

// Focus + titlebar drag. Returns true if a full chrome redraw is needed.
bool on_pointer(int32_t x, int32_t y, uint8_t buttons);

// Hit-test topmost visible window; titlebar=true if y in title strip.
bool hit_test(int32_t x, int32_t y, uint32_t& out_index, bool& titlebar);

bool client_of(uint32_t index, Client& out);
int32_t find_kind(Kind kind);  // -1 if missing

void set_focus(uint32_t index);
void raise(uint32_t index);

uint32_t focused();
Window* window(uint32_t index);
uint32_t window_count();

}  // namespace cxc::wm
