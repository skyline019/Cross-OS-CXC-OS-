#pragma once

#include <stdint.h>

namespace cxc::irq {

using Handler = void (*)(void* context);

// Fixed slot registration for vectors 34..47. One handler per GSI/vector;
// registration and removal must run with IF clear and do not allocate memory.
bool register_gsi(uint32_t gsi, uint16_t flags, Handler handler, void* context,
                  uint8_t& vector);
bool register_isa(uint8_t irq, Handler handler, void* context, uint8_t& vector);

// Registers a message-signaled vector without an IOAPIC GSI. Device code must
// quiesce MSI/MSI-X before calling unregister().
bool register_message(Handler handler, void* context, uint8_t& vector);
bool unregister(uint8_t vector);

void dispatch(uint64_t vector);
uint32_t active_count();
uint64_t unhandled_count();

}  // namespace cxc::irq
