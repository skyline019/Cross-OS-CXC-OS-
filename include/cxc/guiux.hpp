#pragma once

#include <stdint.h>

namespace cxc::guiux {

struct ContractResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t mouse;
    uint32_t windows;
    uint32_t scale;
    uint32_t vt100;
    uint32_t tty;
    uint32_t fb0;
};

// After mini-IDE (GOP required). Synthetic mouse/TTY/fb0 — never blocks.
bool run_automated_contract(ContractResult& result);

}  // namespace cxc::guiux
