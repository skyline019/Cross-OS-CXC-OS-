#include <cxc/ide.hpp>

#include <cxc/cpu.hpp>
#include <cxc/cxcfs.hpp>
#include <cxc/fb.hpp>
#include <cxc/fs.hpp>
#include <cxc/log.hpp>
#include <cxc/mouse.hpp>
#include <cxc/ps2.hpp>
#include <cxc/syscall.hpp>
#include <cxc/user.hpp>
#include <cxc/wm.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::ide {
namespace {

constexpr uint32_t color_bg = 0xff101820;
constexpr uint32_t color_fg = 0xffe8eef4;
constexpr uint32_t color_accent = 0xff7ec8e3;
constexpr uint32_t color_ok = 0xff6bcb77;
constexpr uint32_t color_err = 0xffe85d5d;
constexpr uint32_t color_cursor = 0xfff0c14e;
constexpr uint32_t color_term_bg = 0xff0a1218;
constexpr uint32_t color_term_fg = 0xffb8c8d4;
constexpr uint32_t color_sel = 0xff2a5080;
constexpr uint32_t color_tab = 0xff1e3a5f;
constexpr uint32_t color_tab_on = 0xff3d5a80;
constexpr uint32_t color_dim = 0xff6a7a8a;

constexpr size_t buf_cap = 32 * 1024;
constexpr uint32_t max_tabs = 6;
constexpr uint32_t max_tree = 96;

constexpr char default_src[] =
    "CXC_A2\n"
    "fn main() {\n"
    "return 42;\n"
    "}\n";

struct Tab {
    bool used;
    bool dirty;
    char path[fs::max_path];
    char data[buf_cap];
    size_t len;
    size_t cursor;
    size_t view_row;
};

Tab tabs[max_tabs]{};
uint32_t active_tab = 0;

cxcfs::DirEnt tree_ents[max_tree]{};
uint32_t tree_count = 0;
uint32_t tree_sel = 0;
uint32_t tree_view = 0;
// CXCFS-relative cwd (no disk/ prefix). Empty = volume root.
char tree_rel[fs::max_path]{};
size_t tree_rel_len = 0;

constexpr size_t term_cap = 8192;
char term_buf[term_cap];
size_t term_len = 0;
size_t term_view_row = 0;

bool hold_flag = false;
const uint8_t* g_cc = nullptr;
size_t g_cc_size = 0;
char status_msg[80]{};
uint32_t status_fg = color_fg;

bool left_click_edge = false;

void copy_cstr(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (src[i] != 0 && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

size_t cstrlen(const char* s) {
    size_t n = 0;
    if (s == nullptr) {
        return 0;
    }
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

bool streq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

bool ends_with_c(const char* path) {
    const size_t n = cstrlen(path);
    return n >= 2 && path[n - 2] == '.' && path[n - 1] == 'c';
}

bool format_u32(char* dst, size_t cap, uint32_t value) {
    char tmp[16];
    size_t n = 0;
    if (cap == 0) {
        return false;
    }
    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value > 0 && n + 1 < sizeof(tmp)) {
            tmp[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    if (n >= cap) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        dst[i] = tmp[n - 1 - i];
    }
    dst[n] = 0;
    return true;
}

bool append_cstr(char* dst, size_t cap, const char* src) {
    size_t n = 0;
    while (n < cap && dst[n] != 0) {
        ++n;
    }
    if (src == nullptr) {
        return n < cap;
    }
    while (*src != 0 && n + 1 < cap) {
        dst[n++] = *src++;
    }
    if (n < cap) {
        dst[n] = 0;
        return true;
    }
    return false;
}

void set_status(const char* msg, uint32_t fg) {
    copy_cstr(status_msg, sizeof(status_msg), msg);
    status_fg = fg;
}

void term_clear() {
    term_len = 0;
    term_buf[0] = 0;
    term_view_row = 0;
    syscall::ide_capture_reset();
}

void term_append(const char* text) {
    if (text == nullptr) {
        return;
    }
    while (*text != 0 && term_len + 1 < term_cap) {
        term_buf[term_len++] = *text++;
    }
    term_buf[term_len] = 0;
}

void term_line(const char* text) {
    term_append(text);
    term_append("\n");
}

void term_line_u32(const char* prefix, uint32_t value) {
    char num[16];
    term_append(prefix);
    if (format_u32(num, sizeof(num), value)) {
        term_append(num);
    }
    term_append("\n");
}

void term_pull_capture() {
    static char scratch[term_cap];
    const size_t n = syscall::ide_capture_copy(scratch, sizeof(scratch));
    if (n == 0) {
        return;
    }
    term_append("--- stdout/stderr ---\n");
    term_append(scratch);
    if (n > 0 && scratch[n - 1] != '\n') {
        term_append("\n");
    }
}

void term_scroll_end() {
    uint32_t lines = 0;
    for (size_t i = 0; i < term_len; ++i) {
        if (term_buf[i] == '\n') {
            ++lines;
        }
    }
    if (term_len > 0 && term_buf[term_len - 1] != '\n') {
        ++lines;
    }
    term_view_row = 0;
    // Pin near bottom when painted (uses client rows later).
    (void)lines;
}

void term_report_artifacts(const user::IdeRebuildResult& rb) {
    term_line("--- artifacts ---");
    term_line_u32("src bytes=", rb.src_bytes);
    term_line_u32("out bytes=", rb.out_bytes);
    term_line("--- result ---");
    term_line_u32("stage=", rb.stage);
    term_line_u32("built=", rb.built);
    term_line_u32("ran=", rb.ran);
    term_line_u32("exit_code=",
                  static_cast<uint32_t>(rb.exit_code & 0xffffffffu));
    if (rb.failure != 0) {
        term_line_u32("failure=", rb.failure);
    }
}

void disk_path_from_rel(char* out, size_t cap, const char* rel) {
    copy_cstr(out, cap, "disk/");
    if (rel != nullptr && rel[0] != 0) {
        (void)append_cstr(out, cap, rel);
    }
}

void join_rel(char* out, size_t cap, const char* base, size_t base_len,
              const char* name) {
    size_t i = 0;
    if (out == nullptr || cap == 0) {
        return;
    }
    for (; i < base_len && i + 1 < cap; ++i) {
        out[i] = base[i];
    }
    if (base_len > 0 && i + 1 < cap) {
        out[i++] = '/';
    }
    if (name != nullptr) {
        while (*name != 0 && i + 1 < cap) {
            out[i++] = *name++;
        }
    }
    out[i] = 0;
}

void tree_refresh() {
    tree_count = 0;
    tree_sel = 0;
    tree_view = 0;
    const int32_t n =
        cxcfs::list_dir_rel(tree_rel, tree_rel_len, tree_ents, max_tree);
    if (n > 0) {
        tree_count = static_cast<uint32_t>(n);
    }
}

Tab* active() {
    if (active_tab >= max_tabs || !tabs[active_tab].used) {
        return nullptr;
    }
    return &tabs[active_tab];
}

int32_t find_tab_path(const char* path) {
    for (uint32_t i = 0; i < max_tabs; ++i) {
        if (tabs[i].used && streq(tabs[i].path, path)) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t alloc_tab() {
    for (uint32_t i = 0; i < max_tabs; ++i) {
        if (!tabs[i].used) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void tab_clear(Tab& t) {
    t = {};
}

bool load_file_into(Tab& t, const char* disk_path) {
    const int64_t fd = fs::open_path(disk_path, fs::o_rdonly, 0);
    if (fd < 0) {
        return false;
    }
    const int64_t got =
        fs::read_fd(static_cast<uint32_t>(fd), t.data, buf_cap - 1);
    (void)fs::close_fd(static_cast<uint32_t>(fd));
    if (got < 0) {
        return false;
    }
    t.len = static_cast<size_t>(got);
    t.data[t.len] = 0;
    t.cursor = t.len;
    t.view_row = 0;
    t.dirty = false;
    copy_cstr(t.path, sizeof(t.path), disk_path);
    t.used = true;
    return true;
}

bool open_path_tab(const char* disk_path) {
    const int32_t existing = find_tab_path(disk_path);
    if (existing >= 0) {
        active_tab = static_cast<uint32_t>(existing);
        return true;
    }
    const int32_t slot = alloc_tab();
    if (slot < 0) {
        set_status("too many tabs (F3 close)", color_err);
        return false;
    }
    Tab& t = tabs[static_cast<uint32_t>(slot)];
    tab_clear(t);
    if (!load_file_into(t, disk_path)) {
        // New empty buffer for missing path.
        copy_cstr(t.path, sizeof(t.path), disk_path);
        t.data[0] = 0;
        t.len = 0;
        t.cursor = 0;
        t.view_row = 0;
        t.dirty = true;
        t.used = true;
    }
    active_tab = static_cast<uint32_t>(slot);
    return true;
}

void seed_default_tab() {
    for (uint32_t i = 0; i < max_tabs; ++i) {
        tab_clear(tabs[i]);
    }
    Tab& t = tabs[0];
    t.used = true;
    t.dirty = true;
    copy_cstr(t.path, sizeof(t.path), "disk/ide_main.c");
    size_t i = 0;
    while (default_src[i] != 0 && i + 1 < buf_cap) {
        t.data[i] = default_src[i];
        ++i;
    }
    t.data[i] = 0;
    t.len = i;
    t.cursor = i;
    t.view_row = 0;
    active_tab = 0;
}

uint32_t cursor_row_col(const Tab& t, uint32_t& out_col) {
    uint32_t row = 0;
    uint32_t col = 0;
    for (size_t i = 0; i < t.cursor && i < t.len; ++i) {
        if (t.data[i] == '\n') {
            ++row;
            col = 0;
        } else {
            ++col;
        }
    }
    out_col = col;
    return row;
}

void ensure_cursor_visible(Tab& t, uint32_t text_rows) {
    if (text_rows == 0) {
        return;
    }
    uint32_t col = 0;
    const uint32_t row = cursor_row_col(t, col);
    (void)col;
    if (row < t.view_row) {
        t.view_row = row;
    } else if (row >= t.view_row + text_rows) {
        t.view_row = row - text_rows + 1;
    }
}

void insert_char(Tab& t, char ch) {
    if (t.len + 1 >= buf_cap) {
        return;
    }
    for (size_t i = t.len; i > t.cursor; --i) {
        t.data[i] = t.data[i - 1];
    }
    t.data[t.cursor] = ch;
    ++t.len;
    ++t.cursor;
    t.data[t.len] = 0;
    t.dirty = true;
}

void backspace(Tab& t) {
    if (t.cursor == 0) {
        return;
    }
    for (size_t i = t.cursor - 1; i < t.len; ++i) {
        t.data[i] = t.data[i + 1];
    }
    --t.cursor;
    --t.len;
    t.data[t.len] = 0;
    t.dirty = true;
}

void move_cursor_vert(Tab& t, int delta) {
    uint32_t col = 0;
    uint32_t row = cursor_row_col(t, col);
    if (delta < 0 && row == 0) {
        return;
    }
    const uint32_t target =
        (delta < 0) ? (row - 1) : (row + 1);
    // Find start of target row.
    uint32_t r = 0;
    size_t i = 0;
    while (i < t.len && r < target) {
        if (t.data[i] == '\n') {
            ++r;
        }
        ++i;
    }
    uint32_t c = 0;
    while (i < t.len && t.data[i] != '\n' && c < col) {
        ++i;
        ++c;
    }
    t.cursor = i;
}

void paint_clipped_line(uint32_t col0, uint32_t row, uint32_t cols,
                        const char* text, uint32_t fg, uint32_t bg) {
    if (cols == 0 || text == nullptr) {
        return;
    }
    for (uint32_t c = 0; c < cols && text[c] != 0 && text[c] != '\n'; ++c) {
        char ch = text[c];
        if (ch < 32 || ch > 126) {
            ch = '.';
        }
        fb::put_glyph(col0 + c, row, ch, fg, bg);
    }
}

void paint_tree(const wm::Client& cl) {
    fb::fill_rect(cl.px, cl.py, cl.pw, cl.ph, color_bg);
    char hdr[80];
    copy_cstr(hdr, sizeof(hdr), "disk/");
    if (tree_rel_len > 0) {
        (void)append_cstr(hdr, sizeof(hdr), tree_rel);
    } else {
        (void)append_cstr(hdr, sizeof(hdr), "(root)");
    }
    paint_clipped_line(cl.col0, cl.row0, cl.cols, hdr, color_accent, color_bg);
    if (cl.rows < 2) {
        return;
    }
    const uint32_t body = cl.rows - 1;
    if (tree_sel < tree_view) {
        tree_view = tree_sel;
    } else if (tree_sel >= tree_view + body) {
        tree_view = tree_sel - body + 1;
    }
    // Synthetic .. entry at top when not at root.
    uint32_t paint_row = 0;
    uint32_t logical = 0;
    if (tree_rel_len > 0) {
        if (tree_view == 0) {
            const bool sel = (tree_sel == 0);
            paint_clipped_line(cl.col0, cl.row0 + 1 + paint_row, cl.cols, "..",
                               sel ? color_bg : color_fg,
                               sel ? color_sel : color_bg);
            ++paint_row;
        }
        logical = 1;
    }
    for (uint32_t i = 0; i < tree_count && paint_row < body; ++i) {
        const uint32_t log_i =
            (tree_rel_len > 0) ? (i + 1) : i;
        if (log_i < tree_view) {
            continue;
        }
        char line[72];
        copy_cstr(line, sizeof(line),
                  tree_ents[i].is_dir ? "[D] " : "    ");
        (void)append_cstr(line, sizeof(line), tree_ents[i].name);
        const bool sel = (tree_sel == log_i);
        paint_clipped_line(cl.col0, cl.row0 + 1 + paint_row, cl.cols, line,
                           sel ? color_bg : color_fg,
                           sel ? color_sel : color_bg);
        ++paint_row;
        (void)logical;
    }
}

uint32_t tree_logical_count() {
    return tree_count + ((tree_rel_len > 0) ? 1u : 0u);
}

void paint_editor(const wm::Client& cl) {
    fb::fill_rect(cl.px, cl.py, cl.pw, cl.ph, color_bg);
    if (cl.rows == 0 || cl.cols == 0) {
        return;
    }
    // Tab strip.
    uint32_t tab_col = cl.col0;
    for (uint32_t i = 0; i < max_tabs && tab_col < cl.col0 + cl.cols; ++i) {
        if (!tabs[i].used) {
            continue;
        }
        const char* p = tabs[i].path;
        const char* base = p;
        for (size_t k = 0; p[k] != 0; ++k) {
            if (p[k] == '/') {
                base = p + k + 1;
            }
        }
        char label[24];
        copy_cstr(label, sizeof(label), base);
        if (tabs[i].dirty) {
            (void)append_cstr(label, sizeof(label), "*");
        }
        const bool on = (i == active_tab);
        const size_t lab_n = cstrlen(label);
        for (size_t c = 0; c < lab_n + 1 && tab_col < cl.col0 + cl.cols; ++c) {
            const char ch = (c == 0) ? '[' : (c == lab_n ? ']' : label[c - 1]);
            fb::put_glyph(tab_col, cl.row0, ch, on ? color_fg : color_dim,
                          on ? color_tab_on : color_tab);
            ++tab_col;
        }
        if (tab_col < cl.col0 + cl.cols) {
            fb::put_glyph(tab_col, cl.row0, ' ', color_fg, color_bg);
            ++tab_col;
        }
    }
    Tab* t = active();
    if (t == nullptr || cl.rows < 2) {
        paint_clipped_line(cl.col0, cl.row0 + 1, cl.cols, "(no file open)",
                           color_dim, color_bg);
        return;
    }
    const uint32_t text_rows = cl.rows - 1;
    ensure_cursor_visible(*t, text_rows);
    size_t idx = 0;
    uint32_t skip = 0;
    while (skip < t->view_row && idx < t->len) {
        if (t->data[idx] == '\n') {
            ++skip;
        }
        ++idx;
    }
    uint32_t cur_col = 0;
    const uint32_t cur_row = cursor_row_col(*t, cur_col);
    for (uint32_t row = 0; row < text_rows; ++row) {
        uint32_t col = 0;
        while (idx < t->len && t->data[idx] != '\n' && col < cl.cols) {
            const bool on_cursor =
                (t->view_row + row == cur_row && col == cur_col);
            char ch = t->data[idx];
            if (ch < 32 || ch > 126) {
                ch = '.';
            }
            fb::put_glyph(cl.col0 + col, cl.row0 + 1 + row, ch,
                          on_cursor ? color_bg : color_fg,
                          on_cursor ? color_cursor : color_bg);
            ++col;
            ++idx;
        }
        if (t->view_row + row == cur_row && col == cur_col && col < cl.cols) {
            fb::put_glyph(cl.col0 + col, cl.row0 + 1 + row, ' ', color_bg,
                          color_cursor);
        }
        if (idx < t->len && t->data[idx] == '\n') {
            ++idx;
        } else if (idx >= t->len) {
            // fall through empty rows
        } else {
            while (idx < t->len && t->data[idx] != '\n') {
                ++idx;
            }
            if (idx < t->len && t->data[idx] == '\n') {
                ++idx;
            }
        }
    }
}

void paint_term(const wm::Client& cl) {
    fb::fill_rect(cl.px, cl.py, cl.pw, cl.ph, color_term_bg);
    if (cl.rows == 0) {
        return;
    }
    // Auto-pin near end.
    uint32_t lines = 0;
    for (size_t i = 0; i < term_len; ++i) {
        if (term_buf[i] == '\n') {
            ++lines;
        }
    }
    if (term_len > 0 && (term_len == 0 || term_buf[term_len - 1] != '\n')) {
        ++lines;
    }
    if (lines > cl.rows && term_view_row + cl.rows > lines) {
        // keep
    } else if (lines > cl.rows && term_view_row == 0) {
        term_view_row = lines - cl.rows;
    }
    size_t idx = 0;
    uint32_t skip = 0;
    while (skip < term_view_row && idx < term_len) {
        if (term_buf[idx] == '\n') {
            ++skip;
        }
        ++idx;
    }
    for (uint32_t row = 0; row < cl.rows; ++row) {
        uint32_t col = 0;
        while (idx < term_len && term_buf[idx] != '\n' && col < cl.cols) {
            char ch = term_buf[idx];
            if (ch < 32 || ch > 126) {
                ch = '.';
            }
            fb::put_glyph(cl.col0 + col, cl.row0 + row, ch, color_term_fg,
                          color_term_bg);
            ++col;
            ++idx;
        }
        if (idx < term_len && term_buf[idx] == '\n') {
            ++idx;
        } else if (idx >= term_len) {
            break;
        } else {
            while (idx < term_len && term_buf[idx] != '\n') {
                ++idx;
            }
            if (idx < term_len && term_buf[idx] == '\n') {
                ++idx;
            }
        }
    }
}

void paint_all(int32_t mx, int32_t my) {
    wm::draw_chrome();
    for (uint32_t i = 0; i < wm::window_count(); ++i) {
        wm::Window* w = wm::window(i);
        if (w == nullptr || !w->visible) {
            continue;
        }
        wm::Client cl{};
        if (!wm::client_of(i, cl)) {
            continue;
        }
        if (w->kind == wm::Kind::Tree) {
            paint_tree(cl);
        } else if (w->kind == wm::Kind::Editor || w->kind == wm::Kind::Ide) {
            paint_editor(cl);
        } else if (w->kind == wm::Kind::Term) {
            paint_term(cl);
        }
    }
    // Status in desktop title row right side — reuse bottom of first row.
    if (status_msg[0] != 0) {
        fb::puts(1, 0, status_msg, status_fg, 0xff0b121a);
    }
    wm::draw_pointer(mx, my);
}

wm::Kind focus_kind() {
    wm::Window* w = wm::window(wm::focused());
    return w != nullptr ? w->kind : wm::Kind::Editor;
}

void focus_kind_win(wm::Kind k) {
    const int32_t i = wm::find_kind(k);
    if (i >= 0) {
        wm::raise(static_cast<uint32_t>(i));
    }
}

void tree_activate() {
    const uint32_t n = tree_logical_count();
    if (n == 0) {
        return;
    }
    if (tree_sel >= n) {
        tree_sel = n - 1;
    }
    if (tree_rel_len > 0 && tree_sel == 0) {
        // Go up.
        size_t slash = tree_rel_len;
        for (size_t i = 0; i < tree_rel_len; ++i) {
            if (tree_rel[i] == '/') {
                slash = i;
            }
        }
        if (slash >= tree_rel_len) {
            tree_rel_len = 0;
            tree_rel[0] = 0;
        } else {
            tree_rel_len = slash;
            tree_rel[tree_rel_len] = 0;
        }
        tree_refresh();
        set_status("cd ..", color_fg);
        return;
    }
    const uint32_t ent_i =
        (tree_rel_len > 0) ? (tree_sel - 1) : tree_sel;
    if (ent_i >= tree_count) {
        return;
    }
    cxcfs::DirEnt& e = tree_ents[ent_i];
    if (e.is_dir) {
        char next[fs::max_path];
        join_rel(next, sizeof(next), tree_rel, tree_rel_len, e.name);
        copy_cstr(tree_rel, sizeof(tree_rel), next);
        tree_rel_len = cstrlen(tree_rel);
        tree_refresh();
        set_status("entered dir", color_fg);
        return;
    }
    char disk_path[fs::max_path];
    char rel[fs::max_path];
    join_rel(rel, sizeof(rel), tree_rel, tree_rel_len, e.name);
    disk_path_from_rel(disk_path, sizeof(disk_path), rel);
    if (open_path_tab(disk_path)) {
        focus_kind_win(wm::Kind::Editor);
        set_status("opened", color_ok);
    }
}

void action_save() {
    Tab* t = active();
    if (t == nullptr) {
        set_status("no buffer", color_err);
        return;
    }
    uint32_t fail = 0;
    term_clear();
    term_line("$ save");
    term_line(t->path);
    if (!user::ide_save_path(t->path, t->data, t->len, fail)) {
        term_line_u32("SAVE FAIL code=", fail);
        set_status("save fail", color_err);
        term_scroll_end();
        return;
    }
    t->dirty = false;
    term_line_u32("wrote bytes=", static_cast<uint32_t>(t->len));
    term_line("SAVE OK");
    tree_refresh();
    set_status("saved", color_ok);
    term_scroll_end();
}

void action_close() {
    if (active_tab >= max_tabs || !tabs[active_tab].used) {
        return;
    }
    tab_clear(tabs[active_tab]);
    // Pick another open tab.
    active_tab = 0;
    for (uint32_t i = 0; i < max_tabs; ++i) {
        if (tabs[i].used) {
            active_tab = i;
            set_status("closed tab", color_fg);
            return;
        }
    }
    seed_default_tab();
    set_status("closed; new buffer", color_fg);
}

void action_open_selected() {
    focus_kind_win(wm::Kind::Tree);
    tree_activate();
}

void action_next_tab() {
    for (uint32_t step = 1; step <= max_tabs; ++step) {
        const uint32_t i = (active_tab + step) % max_tabs;
        if (tabs[i].used) {
            active_tab = i;
            focus_kind_win(wm::Kind::Editor);
            set_status(tabs[i].path, color_fg);
            return;
        }
    }
}

void action_build() {
    if (g_cc == nullptr || g_cc_size == 0) {
        set_status("no cc image", color_err);
        return;
    }
    Tab* t = active();
    if (t == nullptr) {
        set_status("no buffer", color_err);
        return;
    }
    term_clear();
    term_line("$ save + build + run");
    term_line(t->path);
    uint32_t fail = 0;
    if (!user::ide_save_path(t->path, t->data, t->len, fail)) {
        term_line_u32("SAVE FAIL=", fail);
        set_status("save fail", color_err);
        return;
    }
    t->dirty = false;
    const char* src = ends_with_c(t->path) ? t->path : "disk/ide_main.c";
    if (!ends_with_c(t->path)) {
        term_line("(not .c — building disk/ide_main.c)");
        (void)user::ide_save_path("disk/ide_main.c", t->data, t->len, fail);
        src = "disk/ide_main.c";
    }
    term_line("$ disk/cc -o disk/ide_out.elf");
    term_line(src);
    syscall::ide_capture_reset();
    syscall::ide_capture_set(true);
    user::IdeRebuildResult rb{};
    const bool ok = user::ide_build_and_run(g_cc, g_cc_size, rb, src);
    syscall::ide_capture_set(false);
    term_pull_capture();
    rb.saved = 1;
    term_report_artifacts(rb);
    term_line(ok ? "BUILD+RUN OK" : "BUILD/RUN FAILED");
    term_scroll_end();
    tree_refresh();
    focus_kind_win(wm::Kind::Term);
    char st[64];
    char num[16];
    if (ok) {
        copy_cstr(st, sizeof(st), "ok exit=");
        if (format_u32(num, sizeof(num),
                       static_cast<uint32_t>(rb.exit_code & 0xffffffffu))) {
            (void)append_cstr(st, sizeof(st), num);
        }
        set_status(st, color_ok);
    } else {
        copy_cstr(st, sizeof(st), "fail f=");
        if (format_u32(num, sizeof(num), rb.failure)) {
            (void)append_cstr(st, sizeof(st), num);
        }
        set_status(st, color_err);
    }
}

void action_run() {
    term_clear();
    term_line("$ run disk/ide_out.elf");
    syscall::ide_capture_reset();
    syscall::ide_capture_set(true);
    user::IdeRebuildResult rb{};
    const bool ok = user::ide_run_out(rb);
    syscall::ide_capture_set(false);
    term_pull_capture();
    term_report_artifacts(rb);
    term_line(ok ? "RUN OK" : "RUN FAILED");
    term_scroll_end();
    focus_kind_win(wm::Kind::Term);
    set_status(ok ? "run ok" : "run fail", ok ? color_ok : color_err);
}

void handle_click(int32_t x, int32_t y) {
    uint32_t hit = 0;
    bool tb = false;
    if (!wm::hit_test(x, y, hit, tb) || tb) {
        return;
    }
    wm::Window* w = wm::window(hit);
    if (w == nullptr) {
        return;
    }
    wm::Client cl{};
    if (!wm::client_of(hit, cl)) {
        return;
    }
    const uint32_t gw = fb::glyph_w();
    const uint32_t gh = fb::glyph_h();
    if (gw == 0 || gh == 0) {
        return;
    }
    const uint32_t col = static_cast<uint32_t>(x) / gw;
    const uint32_t row = static_cast<uint32_t>(y) / gh;
    if (col < cl.col0 || row < cl.row0) {
        return;
    }
    const uint32_t lc = col - cl.col0;
    const uint32_t lr = row - cl.row0;
    (void)lc;
    if (w->kind == wm::Kind::Tree) {
        if (lr == 0) {
            return;
        }
        const uint32_t idx = tree_view + (lr - 1);
        if (idx < tree_logical_count()) {
            tree_sel = idx;
        }
        // Double-purpose: second click activates — single click select only;
        // Enter/F4 opens. Quick UX: click selects; click again on same opens.
        static uint32_t last_sel = 0xffffffffu;
        if (last_sel == tree_sel) {
            tree_activate();
            last_sel = 0xffffffffu;
        } else {
            last_sel = tree_sel;
        }
        return;
    }
    if (w->kind == wm::Kind::Editor || w->kind == wm::Kind::Ide) {
        // Click tab strip.
        if (lr == 0) {
            uint32_t tab_col = 0;
            for (uint32_t i = 0; i < max_tabs; ++i) {
                if (!tabs[i].used) {
                    continue;
                }
                const char* p = tabs[i].path;
                const char* base = p;
                for (size_t k = 0; p[k] != 0; ++k) {
                    if (p[k] == '/') {
                        base = p + k + 1;
                    }
                }
                const uint32_t width =
                    static_cast<uint32_t>(cstrlen(base)) + 3 +
                    (tabs[i].dirty ? 1u : 0u);
                if (lc >= tab_col && lc < tab_col + width) {
                    active_tab = i;
                    set_status(tabs[i].path, color_fg);
                    return;
                }
                tab_col += width + 1;
            }
            return;
        }
        Tab* t = active();
        if (t == nullptr) {
            return;
        }
        const uint32_t target_row = t->view_row + (lr - 1);
        uint32_t r = 0;
        size_t i = 0;
        while (i < t->len && r < target_row) {
            if (t->data[i] == '\n') {
                ++r;
            }
            ++i;
        }
        uint32_t c = 0;
        while (i < t->len && t->data[i] != '\n' && c < lc) {
            ++i;
            ++c;
        }
        t->cursor = i;
    }
}

void cycle_focus_win() {
    const uint32_t n = wm::window_count();
    if (n == 0) {
        return;
    }
    wm::raise((wm::focused() + 1) % n);
}

}  // namespace

bool hold_requested() {
    return hold_flag;
}

void set_hold_requested(bool value) {
    hold_flag = value;
}

bool run_automated_contract(const uint8_t* cc_image, size_t cc_size,
                            ContractResult& result) {
    result = {};
    g_cc = cc_image;
    g_cc_size = cc_size;
    result.stage = 1;

    if (!fb::initialize()) {
        result.failure = 1;
        return false;
    }
    fb::clear(color_bg);
    fb::puts(2, 2, "CXC/OS GUI_FB", color_accent, color_bg);
    fb::puts(2, 4, "ONBOX_GUIFB_PASS", color_ok, color_bg);
    log::line("ONBOX_GUIFB_PASS");
    result.guifb = 1;
    result.stage = 2;

    cpu::disable_interrupts();
    const bool kbd = ps2::initialize();
    cpu::enable_interrupts();
    if (!kbd || !ps2::ready()) {
        result.failure = 2;
        return false;
    }
    ps2::inject_char('A');
    ps2::inject_char('B');
    ps2::inject_key(ps2::Key::Enter);
    ps2::Event ev{};
    uint32_t got = 0;
    while (ps2::poll(ev)) {
        ++got;
    }
    if (got < 3) {
        result.failure = 3;
        return false;
    }
    fb::puts(2, 6, "ONBOX_GUIKBD_PASS", color_ok, color_bg);
    log::line("ONBOX_GUIKBD_PASS");
    result.guikbd = 1;
    result.stage = 3;

    seed_default_tab();
    tree_rel_len = 0;
    tree_rel[0] = 0;
    tree_refresh();
    term_clear();
    term_line("$ auto rebuild");
    if (cc_image == nullptr || cc_size == 0) {
        result.failure = 4;
        return false;
    }
    Tab* t = active();
    syscall::ide_capture_reset();
    syscall::ide_capture_set(true);
    user::IdeRebuildResult rb{};
    if (t == nullptr ||
        !user::run_ide_rebuild(t->data, t->len, cc_image, cc_size, rb)) {
        syscall::ide_capture_set(false);
        result.failure = 10 + rb.failure;
        result.stage = 20 + rb.stage;
        result.exit_code = rb.exit_code;
        result.saved = rb.saved;
        result.built = rb.built;
        result.ran = rb.ran;
        return false;
    }
    syscall::ide_capture_set(false);
    term_pull_capture();
    term_report_artifacts(rb);
    result.saved = rb.saved;
    result.built = rb.built;
    result.ran = rb.ran;
    result.exit_code = rb.exit_code;
    result.stage = 4;
    fb::puts(2, 8, "ONBOX_IDE_PASS", color_ok, color_bg);
    log::line("ONBOX_IDE_PASS");
    return true;
}

void run_interactive() {
    if (!fb::ready() || g_cc == nullptr) {
        return;
    }
    seed_default_tab();
    tree_rel_len = 0;
    tree_rel[0] = 0;
    tree_refresh();
    term_clear();
    term_line("Multi-window IDE ready.");
    term_line("Files: Enter/F4 open  F7 refresh  .. = parent");
    term_line("Tabs: F6 next  F3 close  F2 save  F5 build  F9 run");
    term_line("Tab=cycle windows  drag titlebar  click to focus");

    if (!wm::initialize()) {
        return;
    }
    wm::set_font_scale(1);
    wm::layout_ide();

    cpu::disable_interrupts();
    (void)mouse::initialize();
    cpu::enable_interrupts();
    if (fb::info().width > 0 && fb::info().height > 0) {
        mouse::set_bounds(static_cast<int32_t>(fb::info().width - 1),
                          static_cast<int32_t>(fb::info().height - 1));
    }

    set_status("edit mode", color_fg);
    mouse::State ms = mouse::state();
    paint_all(ms.x, ms.y);
    cpu::enable_interrupts();
    left_click_edge = false;

    for (;;) {
        mouse::poll_hw();
        ms = mouse::state();
        const bool chrome = wm::on_pointer(ms.x, ms.y, ms.buttons);
        const bool left = (ms.buttons & 1) != 0;
        if (left && !left_click_edge) {
            handle_click(ms.x, ms.y);
        }
        left_click_edge = left;
        if (chrome) {
            paint_all(ms.x, ms.y);
        }

        ps2::Event ev{};
        bool keyed = false;
        while (ps2::poll(ev)) {
            keyed = true;
            if (ev.key == ps2::Key::Escape) {
                set_status("quit", color_fg);
                paint_all(ms.x, ms.y);
                return;
            }
            if (ev.key == ps2::Key::F2) {
                action_save();
                continue;
            }
            if (ev.key == ps2::Key::F3) {
                action_close();
                continue;
            }
            if (ev.key == ps2::Key::F4) {
                action_open_selected();
                continue;
            }
            if (ev.key == ps2::Key::F5) {
                action_build();
                continue;
            }
            if (ev.key == ps2::Key::F6) {
                action_next_tab();
                continue;
            }
            if (ev.key == ps2::Key::F7) {
                tree_refresh();
                set_status("tree refreshed", color_fg);
                continue;
            }
            if (ev.key == ps2::Key::F9) {
                action_run();
                continue;
            }
            if (ev.key == ps2::Key::Tab) {
                cycle_focus_win();
                continue;
            }
            if (ev.key == ps2::Key::PageUp) {
                if (term_view_row > 0) {
                    --term_view_row;
                }
                continue;
            }
            if (ev.key == ps2::Key::PageDown) {
                ++term_view_row;
                continue;
            }

            const wm::Kind fk = focus_kind();
            if (fk == wm::Kind::Tree) {
                if (ev.key == ps2::Key::Up && tree_sel > 0) {
                    --tree_sel;
                } else if (ev.key == ps2::Key::Down &&
                           tree_sel + 1 < tree_logical_count()) {
                    ++tree_sel;
                } else if (ev.key == ps2::Key::Enter) {
                    tree_activate();
                }
                continue;
            }
            if (fk == wm::Kind::Term) {
                if (ev.key == ps2::Key::Up && term_view_row > 0) {
                    --term_view_row;
                } else if (ev.key == ps2::Key::Down) {
                    ++term_view_row;
                }
                continue;
            }

            Tab* t = active();
            if (t == nullptr) {
                continue;
            }
            if (ev.key == ps2::Key::Enter) {
                insert_char(*t, '\n');
            } else if (ev.key == ps2::Key::Backspace) {
                backspace(*t);
            } else if (ev.key == ps2::Key::Char) {
                insert_char(*t, ev.ch);
            } else if (ev.key == ps2::Key::Left && t->cursor > 0) {
                --t->cursor;
            } else if (ev.key == ps2::Key::Right && t->cursor < t->len) {
                ++t->cursor;
            } else if (ev.key == ps2::Key::Up) {
                move_cursor_vert(*t, -1);
            } else if (ev.key == ps2::Key::Down) {
                move_cursor_vert(*t, 1);
            }
        }

        if (keyed || chrome || left) {
            paint_all(ms.x, ms.y);
        } else {
            // Still redraw pointer occasionally.
            paint_all(ms.x, ms.y);
            cpu::relax();
        }
    }
}

}  // namespace cxc::ide
