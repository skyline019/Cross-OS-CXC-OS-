#include <cxc/vt100.hpp>

#include <cxc/fb.hpp>

namespace cxc::vt100 {
namespace {

void clamp_cursor(Screen& scr) {
    if (scr.cx >= scr.cols) {
        scr.cx = scr.cols ? scr.cols - 1 : 0;
    }
    if (scr.cy >= scr.rows) {
        scr.cy = scr.rows ? scr.rows - 1 : 0;
    }
}

void put(Screen& scr, char ch) {
    if (scr.cy >= scr.rows || scr.cx >= scr.cols) {
        return;
    }
    scr.cells[scr.cy][scr.cx] = ch;
    ++scr.cx;
    if (scr.cx >= scr.cols) {
        scr.cx = 0;
        if (scr.cy + 1 < scr.rows) {
            ++scr.cy;
        }
    }
}

void parse_csi(Screen& scr, const int* params, uint32_t nparams, char final_ch) {
    auto p = [&](uint32_t idx, int defv) -> int {
        if (idx < nparams && params[idx] > 0) {
            return params[idx];
        }
        return defv;
    };
    switch (final_ch) {
    case 'H':
    case 'f': {
        const int row = p(0, 1);
        const int col = p(1, 1);
        scr.cy = static_cast<uint32_t>(row > 0 ? row - 1 : 0);
        scr.cx = static_cast<uint32_t>(col > 0 ? col - 1 : 0);
        clamp_cursor(scr);
        break;
    }
    case 'J': {
        const int mode = p(0, 0);
        if (mode == 2 || mode == 0) {
            for (uint32_t r = 0; r < scr.rows; ++r) {
                for (uint32_t c = 0; c < scr.cols; ++c) {
                    scr.cells[r][c] = ' ';
                }
            }
            if (mode == 2) {
                scr.cx = 0;
                scr.cy = 0;
            }
        }
        break;
    }
    case 'K': {
        for (uint32_t c = scr.cx; c < scr.cols; ++c) {
            scr.cells[scr.cy][c] = ' ';
        }
        break;
    }
    case 'A': {
        const uint32_t n = static_cast<uint32_t>(p(0, 1));
        scr.cy = (scr.cy > n) ? scr.cy - n : 0;
        break;
    }
    case 'B': {
        scr.cy += static_cast<uint32_t>(p(0, 1));
        clamp_cursor(scr);
        break;
    }
    case 'C': {
        scr.cx += static_cast<uint32_t>(p(0, 1));
        clamp_cursor(scr);
        break;
    }
    case 'D': {
        const uint32_t n = static_cast<uint32_t>(p(0, 1));
        scr.cx = (scr.cx > n) ? scr.cx - n : 0;
        break;
    }
    case 'm': {
        if (nparams == 0) {
            scr.fg = 0xffe8eef4;
            scr.bg = 0xff101820;
            break;
        }
        for (uint32_t i = 0; i < nparams; ++i) {
            const int code = params[i];
            if (code == 0) {
                scr.fg = 0xffe8eef4;
                scr.bg = 0xff101820;
            } else if (code == 31) {
                scr.fg = 0xffe85d5d;
            } else if (code == 32) {
                scr.fg = 0xff6bcb77;
            } else if (code == 36) {
                scr.fg = 0xff7ec8e3;
            }
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

void reset(Screen& scr, uint32_t cols, uint32_t rows) {
    if (cols == 0 || cols > cols_max) {
        cols = 80;
    }
    if (rows == 0 || rows > rows_max) {
        rows = 24;
    }
    scr.cols = cols;
    scr.rows = rows;
    scr.cx = 0;
    scr.cy = 0;
    scr.fg = 0xffe8eef4;
    scr.bg = 0xff101820;
    for (uint32_t r = 0; r < rows_max; ++r) {
        for (uint32_t c = 0; c < cols_max; ++c) {
            scr.cells[r][c] = ' ';
        }
    }
}

void write(Screen& scr, const char* data, size_t len) {
    if (data == nullptr) {
        return;
    }
    enum class St : uint32_t { Norm, Esc, Csi };
    St st = St::Norm;
    int params[8]{};
    uint32_t nparams = 0;
    int cur_param = 0;
    bool in_param = false;

    for (size_t i = 0; i < len; ++i) {
        const char ch = data[i];
        if (st == St::Norm) {
            if (ch == '\x1b') {
                st = St::Esc;
            } else if (ch == '\n') {
                scr.cx = 0;
                if (scr.cy + 1 < scr.rows) {
                    ++scr.cy;
                }
            } else if (ch == '\r') {
                scr.cx = 0;
            } else if (ch == '\b') {
                if (scr.cx > 0) {
                    --scr.cx;
                }
            } else if (ch >= 0x20) {
                put(scr, ch);
            }
            continue;
        }
        if (st == St::Esc) {
            if (ch == '[') {
                st = St::Csi;
                nparams = 0;
                cur_param = 0;
                in_param = false;
            } else {
                st = St::Norm;
            }
            continue;
        }
        // CSI
        if (ch >= '0' && ch <= '9') {
            cur_param = cur_param * 10 + (ch - '0');
            in_param = true;
            continue;
        }
        if (ch == ';') {
            if (nparams < 8) {
                params[nparams++] = in_param ? cur_param : 0;
            }
            cur_param = 0;
            in_param = false;
            continue;
        }
        if (in_param && nparams < 8) {
            params[nparams++] = cur_param;
        }
        parse_csi(scr, params, nparams, ch);
        st = St::Norm;
    }
}

void render(const Screen& scr, uint32_t origin_col, uint32_t origin_row) {
    if (!fb::ready()) {
        return;
    }
    for (uint32_t r = 0; r < scr.rows; ++r) {
        for (uint32_t c = 0; c < scr.cols; ++c) {
            fb::put_glyph(origin_col + c, origin_row + r, scr.cells[r][c],
                          scr.fg, scr.bg);
        }
    }
}

}  // namespace cxc::vt100
