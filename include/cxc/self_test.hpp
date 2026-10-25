#pragma once

namespace cxc::self_test {

// Returns only when every test passes. A failed invariant calls log::panic.
void run();

}  // namespace cxc::self_test
