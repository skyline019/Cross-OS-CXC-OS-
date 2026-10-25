#include <cxc/cpu.hpp>

#include <cxc/apic.hpp>
#include <cxc/ioapic.hpp>
#include <cxc/irq.hpp>
#include <cxc/log.hpp>
#include <cxc/runtime_test.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/smp.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

namespace {

struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct Idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static_assert(sizeof(IdtEntry) == 16);

alignas(16) IdtEntry idt[256];
volatile bool breakpoint_seen = false;

void set_gate(uint8_t vector, uint64_t handler) {
    IdtEntry& entry = idt[vector];
    entry.offset_low = static_cast<uint16_t>(handler & 0xffff);
    entry.selector = 0x08;
    entry.ist = 0;
    entry.type_attributes = 0x8e;  // Present, ring 0, 64-bit interrupt gate.
    entry.offset_middle = static_cast<uint16_t>((handler >> 16) & 0xffff);
    entry.offset_high = static_cast<uint32_t>((handler >> 32) & 0xffffffff);
    entry.reserved = 0;
}

}  // namespace

extern "C" const uint64_t isr_stub_table[32];
extern "C" const uint64_t irq_stub_table[16];
extern "C" void isr48();
extern "C" void isr49();
extern "C" void isr50();
extern "C" void isr51();
extern "C" void isr255();

extern "C" cxc::cpu::InterruptFrame* interrupt_dispatch(
    cxc::cpu::InterruptFrame* frame) {
    if (frame->vector == 3) {
        breakpoint_seen = true;
        cxc::log::line("[exception] breakpoint reached ring-0 handler");
        return frame;
    }
    if (frame->vector == cxc::cpu::timer_vector) {
        return cxc::scheduler::on_timer(frame);
    }
    if (frame->vector == cxc::cpu::ioapic_test_vector) {
        cxc::ioapic::on_interrupt();
        return frame;
    }
    if (frame->vector == cxc::cpu::ipi_vector) {
        cxc::smp::on_ipi();
        cxc::apic::end_of_interrupt();
        return frame;
    }
    if (frame->vector == cxc::cpu::scheduler_ipi_vector) {
        cxc::smp::on_scheduler_ipi();
        cxc::apic::end_of_interrupt();
        return frame;
    }
    if (frame->vector == cxc::cpu::scheduler_wakeup_vector) {
        cxc::scheduler::on_wakeup_ipi();
        cxc::apic::end_of_interrupt();
        return frame;
    }
    if (frame->vector == cxc::cpu::tlb_shootdown_vector) {
        cxc::vmm::on_tlb_shootdown();
        cxc::apic::end_of_interrupt();
        return frame;
    }
    if (frame->vector >= cxc::cpu::external_vector_begin &&
        frame->vector < cxc::cpu::external_vector_end) {
        cxc::irq::dispatch(frame->vector);
        return frame;
    }
    if (frame->vector == cxc::cpu::spurious_vector) {
        // Intel specifies no EOI for the spurious-interrupt vector.
        cxc::apic::note_spurious();
        return frame;
    }
    if (frame->vector == 14 && cxc::runtime_test::on_page_fault(*frame)) {
        return frame;
    }

    cxc::log::write("fatal CPU exception vector: ");
    cxc::log::decimal(frame->vector);
    cxc::log::write(", error: ");
    cxc::log::hex(frame->error_code);
    cxc::log::write(", RIP: ");
    cxc::log::hex(frame->rip);
    if (frame->vector == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        cxc::log::write(", CR2: ");
        cxc::log::hex(cr2);
    }
    cxc::log::write('\n');
    cxc::log::panic("unhandled CPU exception");
}

namespace cxc::cpu {

void initialize_idt() {
    for (size_t index = 0; index < 256; ++index) {
        idt[index] = IdtEntry{};
    }
    for (uint8_t vector = 0; vector < 32; ++vector) {
        set_gate(vector, isr_stub_table[vector]);
    }
    for (uint8_t vector = 32; vector < 48; ++vector) {
        set_gate(vector, irq_stub_table[vector - 32]);
    }
    set_gate(static_cast<uint8_t>(ipi_vector),
             reinterpret_cast<uint64_t>(&isr48));
    set_gate(static_cast<uint8_t>(scheduler_ipi_vector),
             reinterpret_cast<uint64_t>(&isr49));
    set_gate(static_cast<uint8_t>(scheduler_wakeup_vector),
             reinterpret_cast<uint64_t>(&isr50));
    set_gate(static_cast<uint8_t>(tlb_shootdown_vector),
             reinterpret_cast<uint64_t>(&isr51));
    set_gate(static_cast<uint8_t>(spurious_vector),
             reinterpret_cast<uint64_t>(&isr255));
    load_idt();
}

void load_idt() {
    const Idtr descriptor = {static_cast<uint16_t>(sizeof(idt) - 1),
                             reinterpret_cast<uint64_t>(idt)};
    __asm__ volatile("cli; lidt %0" : : "m"(descriptor) : "memory");
}

bool breakpoint_was_seen() {
    return breakpoint_seen;
}

}  // namespace cxc::cpu
