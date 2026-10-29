#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::vt100 {

constexpr uint32_t cols_max = 120;
constexpr uint32_t rows_max = 40;

struct Screen {
    char cells[rows_max][cols_max];
    uint32_t cols;
    uint32_t rows;
    uint32_t cx;
    uint32_t cy;
    uint32_t fg;
    uint32_t bg;
};

void reset(Screen& scr, uint32_t cols, uint32_t rows);
// Feed bytes (supports printable + \n\r\b + CSI subset: H,J,K,m,A,B,C,D).
void write(Screen& scr, const char* data, size_t len);
void render(const Screen& scr, uint32_t origin_col, uint32_t origin_row);

}  // namespace cxc::vt100
