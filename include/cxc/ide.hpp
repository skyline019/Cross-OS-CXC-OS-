#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::ide {

struct ContractResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t guifb;
    uint32_t guikbd;
    uint32_t saved;
    uint32_t built;
    uint32_t ran;
    uint64_t exit_code;
};

// After self-test / CCDRV: FB banner, synthetic kbd, save/build/run on disk/.
// `cc_image` is disk/cc (CCDRV CLI). Never blocks on human input.
// Safe when FB absent (returns false without panicking).
bool run_automated_contract(const uint8_t* cc_image, size_t cc_size,
                            ContractResult& result);

// Multi-window IDE: Files tree + editor tabs + terminal (wm chrome).
// F2 save / F3 close / F4 open / F5 build / F6 next tab / F7 refresh /
// F9 run / Tab cycle windows / Esc quit. Drag titlebars; click to focus.
void run_interactive();

bool hold_requested();
void set_hold_requested(bool value);

}  // namespace cxc::ide
