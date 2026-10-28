#pragma once

#include <stddef.h>
#include <stdint.h>

struct EFI_SYSTEM_TABLE;

namespace cxc::fb {

struct GopInfo {
    uint64_t phys_base = 0;
    uint64_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pixels_per_scanline = 0;
    uint32_t pixel_format = 0;  // EFI_GRAPHICS_PIXEL_FORMAT
};

// Boot Services only: LocateProtocol GOP and stash linear FB info.
// Safe to call when GOP is absent (returns false).
bool capture_gop(EFI_SYSTEM_TABLE* system_table);

// After VMM is up: WC-map FB pages and enable drawing. No-op if capture failed.
bool initialize();

bool ready();
const GopInfo& info();

void clear(uint32_t argb);
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb);
void put_glyph(uint32_t col, uint32_t row, char ch, uint32_t fg, uint32_t bg);
void puts(uint32_t col, uint32_t row, const char* text, uint32_t fg, uint32_t bg);

// Integer glyph scale (1 or 2). Affects put_glyph / cols / rows.
void set_scale(uint32_t scale);
uint32_t scale();

constexpr uint32_t glyph_base_w = 8;
constexpr uint32_t glyph_base_h = 16;
uint32_t glyph_w();
uint32_t glyph_h();

uint32_t cols();
uint32_t rows();

uint8_t* virt_base();
uint64_t mapped_bytes();

}  // namespace cxc::fb
