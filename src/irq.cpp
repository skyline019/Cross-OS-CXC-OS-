#include <cxc/irq.hpp>

#include <cxc/acpi.hpp>
#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/ioapic.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::irq {
namespace {

constexpr size_t slot_count = cpu::external_vector_end - cpu::external_vector_begin;

enum class Source : uint32_t {
    Gsi,
    Message,
};

struct Slot {
    Handler handler;
    void* context;
    uint32_t gsi;
    Source source;
    bool active;
};

Slot slots[slot_count];
uint32_t active_slots = 0;
volatile uint64_t unhandled = 0;

bool valid_vector(uint64_t vector) {
    return vector >= cpu::external_vector_begin &&
           vector < cpu::external_vector_end;
}

}  // namespace

bool register_gsi(uint32_t gsi, uint16_t flags, Handler handler, void* context,
                  uint8_t& vector) {
    if (handler == nullptr || cpu::interrupts_enabled() || !ioapic::initialized()) {
        return false;
    }

    for (size_t index = 0; index < slot_count; ++index) {
        if (slots[index].active) {
            if (slots[index].source == Source::Gsi && slots[index].gsi == gsi) {
                return false;  // Shared IRQ registration is not implemented.
            }
            continue;
        }
        const uint8_t allocated = static_cast<uint8_t>(
            cpu::external_vector_begin + index);
        if (!ioapic::route_gsi(gsi, flags, allocated)) {
            return false;
        }

        slots[index].handler = handler;
        slots[index].context = context;
        slots[index].gsi = gsi;
        slots[index].source = Source::Gsi;
        __atomic_store_n(&slots[index].active, true, __ATOMIC_RELEASE);
        ++active_slots;
        vector = allocated;
        return true;
    }
    return false;
}

bool register_isa(uint8_t irq, Handler handler, void* context, uint8_t& vector) {
    uint16_t flags = 0;
    const uint32_t gsi = acpi::isa_irq_to_gsi(irq, flags);
    return register_gsi(gsi, flags, handler, context, vector);
}

bool register_message(Handler handler, void* context, uint8_t& vector) {
    if (handler == nullptr || cpu::interrupts_enabled() || !apic::initialized()) {
        return false;
    }
    for (size_t index = 0; index < slot_count; ++index) {
        if (slots[index].active) {
            continue;
        }
        slots[index].handler = handler;
        slots[index].context = context;
        slots[index].gsi = 0;
        slots[index].source = Source::Message;
        __atomic_store_n(&slots[index].active, true, __ATOMIC_RELEASE);
        ++active_slots;
        vector = static_cast<uint8_t>(cpu::external_vector_begin + index);
        return true;
    }
    return false;
}

bool unregister(uint8_t vector) {
    if (!valid_vector(vector) || cpu::interrupts_enabled()) {
        return false;
    }
    const size_t index = vector - cpu::external_vector_begin;
    if (!slots[index].active) {
        return false;
    }
    if (slots[index].source == Source::Gsi &&
        !ioapic::mask_gsi(slots[index].gsi)) {
        return false;
    }

    __atomic_store_n(&slots[index].active, false, __ATOMIC_RELEASE);
    slots[index].handler = nullptr;
    slots[index].context = nullptr;
    slots[index].gsi = 0;
    slots[index].source = Source::Gsi;
    --active_slots;
    return true;
}

void dispatch(uint64_t vector) {
    if (valid_vector(vector)) {
        const size_t index = vector - cpu::external_vector_begin;
        if (__atomic_load_n(&slots[index].active, __ATOMIC_ACQUIRE) &&
            slots[index].handler != nullptr) {
            slots[index].handler(slots[index].context);
            apic::end_of_interrupt();
            return;
        }
    }
    __atomic_add_fetch(&unhandled, 1, __ATOMIC_RELAXED);
    apic::end_of_interrupt();
}

uint32_t active_count() {
    return active_slots;
}

uint64_t unhandled_count() {
    return __atomic_load_n(&unhandled, __ATOMIC_RELAXED);
}

}  // namespace cxc::irq
