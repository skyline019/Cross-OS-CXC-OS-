#include <cxc/acpi.hpp>
#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/e1000e.hpp>
#include <cxc/edu.hpp>
#include <cxc/heap.hpp>
#include <cxc/ioapic.hpp>
#include <cxc/irq.hpp>
#include <cxc/log.hpp>
#include <cxc/pci.hpp>
#include <cxc/pmm.hpp>
#include <cxc/runtime_test.hpp>
#include <cxc/self_test.hpp>
#include <cxc/serial.hpp>
#include <cxc/smp.hpp>
#include <cxc/uefi.hpp>
#include <cxc/vmm.hpp>

extern "C" void switch_stack_and_call(
    uint64_t stack_top, void (*entry)(const void*), const void* argument)
    __attribute__((noreturn));

namespace cxc {
namespace {

uefi::BootMemoryMap boot_memory_map;
uint64_t firmware_image_base = 0;
uint64_t firmware_image_size = 0;
uint64_t kernel_stack_physical = 0;
uint64_t acpi_rsdp_address = 0;
uint64_t smp_trampoline_physical = 0;

void require_mapping(uint64_t address, bool writable, bool executable,
                     const char* failure) {
    const vmm::MappingInfo mapping = vmm::inspect_mapping(address);
    if (!mapping.present || mapping.writable != writable ||
        mapping.executable != executable) {
        log::panic(failure);
    }
}

[[noreturn]] void kernel_high_entry(const void* argument) {
    if (argument != nullptr) {
        log::panic("invalid higher-half entry argument");
    }
    log::line("[ok] entered higher-half kernel image");

    // Low mappings still exist here, so a fault remains diagnosable. Install
    // high GDT/IDT before dropping those low mappings.
    cpu::initialize_gdt();
    cpu::initialize_idt();
    if (!vmm::activate_runtime()) {
        log::panic("runtime CR3 activation failed");
    }

    const uint64_t high_entry = reinterpret_cast<uint64_t>(&kernel_high_entry);
    const uint64_t high_data = reinterpret_cast<uint64_t>(&boot_memory_map);
    require_mapping(high_entry, false, true, "kernel text is not RX");
    require_mapping(high_data, true, false, "kernel data is not RW/NX");
    require_mapping(vmm::higher_half_stack_base, true, false,
                    "higher-half stack is not RW/NX");
    require_mapping(vmm::direct_map_base + kernel_stack_physical, true, false,
                    "direct map is not RW/NX");
    if (vmm::is_mapped(vmm::higher_half_stack_guard)) {
        log::panic("higher-half stack guard page is mapped");
    }
    if (vmm::is_mapped(firmware_image_base)) {
        log::panic("low identity kernel mapping remains active");
    }
    log::line("[ok] runtime CR3: low half removed, RX text, NX data/stack/direct map");

    __asm__ volatile("int3");
    if (!cpu::breakpoint_was_seen()) {
        log::panic("breakpoint did not reach the higher-half IDT handler");
    }
    log::line("[ok] higher-half IDT and stack guard checks");

    self_test::run();

    if (!apic::initialize()) {
        log::panic("local APIC initialization failed");
    }
    if (!apic::uses_x2apic()) {
        require_mapping(apic::local_apic_virtual_base, true, false,
                        "local APIC MMIO page is not RW/NX");
    }
    log::write("[ok] ");
    log::write(apic::mode_name());
    log::write(" ID: ");
    log::decimal(apic::apic_id());
    log::write(", calibration ticks/10ms: ");
    log::decimal(apic::calibration_ticks());
    log::write(", periodic ticks/1ms: ");
    log::decimal(apic::periodic_ticks());
    log::write('\n');

    const bool smp_passed = smp::run();
    const smp::TestInfo& smp_info = smp::test_info();
    if (!smp_passed) {
        log::write("[debug] SMP stage: ");
        log::decimal(smp_info.stage);
        log::write(", active CPU: ");
        log::decimal(smp_info.active_cpu);
        log::write(", online: ");
        log::decimal(smp_info.online_processors);
        log::write('/');
        log::decimal(smp_info.expected_processors);
        log::write('\n');
        for (size_t index = 0; index < smp_info.expected_processors; ++index) {
            const smp::CpuResult& result = smp_info.cpus[index];
            log::write("[debug] CPU ");
            log::decimal(index);
            log::write(", APIC: ");
            log::decimal(result.apic_id);
            log::write(", online: ");
            log::decimal(result.online);
            log::write(", parked: ");
            log::decimal(result.parked);
            log::write(", error: ");
            log::decimal(result.error);
            log::write(", IPI: ");
            log::decimal(result.ipi_calls);
            log::write('\n');
        }
        log::panic("SMP AP startup or IPI delivery failed");
    }
    log::write("[ok] SMP processors online: ");
    log::decimal(smp_info.online_processors);
    log::write('/');
    log::decimal(smp_info.expected_processors);
    log::write(", fixed IPIs: ");
    log::decimal(smp_info.total_ipi_calls);
    log::write(", second SIPIs: ");
    log::decimal(smp_info.second_sipi_count);
    log::write(", trampoline: ");
    log::hex(smp_info.trampoline_physical);
    log::write('\n');
    for (size_t index = 0; index < smp_info.expected_processors; ++index) {
        const smp::CpuResult& result = smp_info.cpus[index];
        log::write("[ok] CPU ");
        log::decimal(index);
        log::write(" APIC ID: ");
        log::decimal(result.apic_id);
        log::write(", online: ");
        log::decimal(result.online);
        log::write(", parked: ");
        log::decimal(result.parked);
        log::write(", IPI calls: ");
        log::decimal(result.ipi_calls);
        if (index != 0) {
            log::write(", payload: ");
            log::hex(result.last_ipi_payload);
            log::write(", stack: ");
            log::hex(result.stack_base);
        }
        log::write('\n');
    }

    if (!ioapic::initialize()) {
        log::panic("IOAPIC initialization failed");
    }
    for (uint32_t index = 0; index < acpi::madt().ioapic_count; ++index) {
        require_mapping(ioapic::virtual_base + index * ioapic::virtual_stride,
                        true, false, "IOAPIC MMIO page is not RW/NX");
    }
    log::write("[ok] IOAPIC controllers: ");
    log::decimal(acpi::madt().ioapic_count);
    log::write(", masked RTEs: ");
    log::decimal(ioapic::redirection_entries());
    log::write('\n');
    if (!ioapic::run_irq0_test()) {
        log::panic("IOAPIC PIT IRQ0 delivery test failed");
    }
    log::write("[ok] IOAPIC ISA IRQ0 -> GSI ");
    log::decimal(ioapic::tested_gsi());
    log::write(", test interrupts: ");
    log::decimal(ioapic::test_interrupts());
    log::write('\n');

    if (!pci::enumerate() || pci::class_count(0x06) == 0) {
        log::panic("PCI enumeration or capability walk failed");
    }
    log::write("[ok] PCI functions: ");
    log::decimal(pci::device_count());
    log::write(", MSI capabilities: ");
    log::decimal(pci::msi_capable_count());
    log::write(", MSI-X capabilities: ");
    log::decimal(pci::msix_capable_count());
    log::write('\n');

    uint32_t serial_gsi = 0;
    uint8_t serial_vector = 0;
    if (!serial::run_irq_loopback_test(serial_gsi, serial_vector) ||
        irq::active_count() != 0 || irq::unhandled_count() != 0) {
        log::panic("UART IRQ loopback or IRQ unregister failed");
    }
    log::write("[ok] UART IRQ4 -> GSI ");
    log::decimal(serial_gsi);
    log::write(", vector: ");
    log::decimal(serial_vector);
    log::write(", IRQ calls: ");
    log::decimal(serial::irq_loopback_calls());
    log::write(", bytes: 4");
    log::write('\n');

    if (!edu::run_msi_dma_test() || irq::active_count() != 0 ||
        irq::unhandled_count() != 0) {
        log::panic("EDU MSI/DMA or message-vector unregister failed");
    }
    const edu::TestInfo& edu_info = edu::test_info();
    log::write("[ok] EDU MMIO: ");
    log::hex(edu_info.mmio_physical);
    log::write(", MSI vector: ");
    log::decimal(edu_info.vector);
    log::write(", direct MSI: ");
    log::decimal(edu_info.direct_interrupts);
    log::write(", DMA completion MSI: ");
    log::decimal(edu_info.dma_interrupts);
    log::write('\n');
    log::write("[ok] EDU DMA source: ");
    log::hex(edu_info.source_physical);
    log::write(", destination: ");
    log::hex(edu_info.destination_physical);
    log::write(", verified round-trip bytes: ");
    log::decimal(edu_info.dma_bytes);
    log::write('\n');

    const bool msix_passed = e1000e::run_msix_test();
    const e1000e::TestInfo& msix_info = e1000e::test_info();
    if (!msix_passed) {
        log::write("[debug] e1000e stage: ");
        log::decimal(msix_info.stage);
        log::write(", per-PBA: ");
        log::decimal(msix_info.per_vector_pba_seen);
        log::write(", function-PBA: ");
        log::decimal(msix_info.function_pba_seen);
        log::write('\n');
        log::write("[debug] e1000e packet stage: ");
        log::decimal(msix_info.packet.stage);
        log::write(", RX/TX frames: ");
        log::decimal(msix_info.packet.rx_frames);
        log::write('/');
        log::decimal(msix_info.packet.tx_frames);
        log::write(", heads: ");
        log::decimal(msix_info.packet.rx_head);
        log::write('/');
        log::decimal(msix_info.packet.tx_head);
        log::write(", IRQs: ");
        log::decimal(msix_info.packet.rx_interrupts);
        log::write('/');
        log::decimal(msix_info.packet.tx_interrupts);
        log::write('\n');
        log::write("[debug] e1000e protocol stage: ");
        log::decimal(msix_info.protocol.stage);
        log::write(", ARP req/reply: ");
        log::decimal(msix_info.protocol.arp_requests);
        log::write('/');
        log::decimal(msix_info.protocol.arp_replies);
        log::write(", ICMP req/reply: ");
        log::decimal(msix_info.protocol.icmp_requests);
        log::write('/');
        log::decimal(msix_info.protocol.icmp_replies);
        log::write(", heads: ");
        log::decimal(msix_info.protocol.rx_head);
        log::write('/');
        log::decimal(msix_info.protocol.tx_head);
        log::write(", IRQs: ");
        log::decimal(msix_info.protocol.rx_interrupts);
        log::write('/');
        log::decimal(msix_info.protocol.tx_interrupts);
        log::write('\n');
        log::write("[debug] e1000e UDP stage: ");
        log::decimal(msix_info.udp.stage);
        log::write(", failure: ");
        log::decimal(msix_info.udp.failure);
        log::write(", datagram: ");
        log::decimal(msix_info.udp.datagram);
        log::write(", req/reply: ");
        log::decimal(msix_info.udp.requests);
        log::write('/');
        log::decimal(msix_info.udp.replies);
        log::write(", descriptors: ");
        log::decimal(msix_info.udp.rx_descriptor);
        log::write('/');
        log::decimal(msix_info.udp.tx_descriptor);
        log::write(", last bytes: ");
        log::decimal(msix_info.udp.last_rx_bytes);
        log::write('/');
        log::decimal(msix_info.udp.last_tx_bytes);
        log::write('\n');
        log::write("[debug] e1000e UDP heads/tails: ");
        log::decimal(msix_info.udp.rx_head);
        log::write('/');
        log::decimal(msix_info.udp.tx_head);
        log::write(", ");
        log::decimal(msix_info.udp.rx_tail);
        log::write('/');
        log::decimal(msix_info.udp.tx_tail);
        log::write(", IRQs: ");
        log::decimal(msix_info.udp.rx_interrupts);
        log::write('/');
        log::decimal(msix_info.udp.tx_interrupts);
        log::write(", payload/frame bytes: ");
        log::decimal(msix_info.udp.payload_bytes);
        log::write(", ");
        log::decimal(msix_info.udp.rx_bytes);
        log::write('/');
        log::decimal(msix_info.udp.tx_bytes);
        log::write('\n');
        log::write("[debug] e1000e UDP wraps RXh/TXh/RXt/TXt: ");
        log::decimal(msix_info.udp.rx_head_wraps);
        log::write('/');
        log::decimal(msix_info.udp.tx_head_wraps);
        log::write('/');
        log::decimal(msix_info.udp.rx_tail_wraps);
        log::write('/');
        log::decimal(msix_info.udp.tx_tail_wraps);
        log::write(", recycled: ");
        log::decimal(msix_info.udp.recycled_buffers);
        log::write('\n');
        log::write("[debug] e1000e stress stage: ");
        log::decimal(msix_info.stress.stage);
        log::write(", failure: ");
        log::decimal(msix_info.stress.failure);
        log::write(", req/reply: ");
        log::decimal(msix_info.stress.requests);
        log::write('/');
        log::decimal(msix_info.stress.replies);
        log::write('\n');
        log::write("[debug] e1000e socket_tx stage: ");
        log::decimal(msix_info.socket_tx.stage);
        log::write(", failure: ");
        log::decimal(msix_info.socket_tx.failure);
        log::write("; socket_tx_bp stage: ");
        log::decimal(msix_info.socket_tx_bp.stage);
        log::write(", failure: ");
        log::decimal(msix_info.socket_tx_bp.failure);
        log::write(", posted/rejects: ");
        log::decimal(msix_info.socket_tx_bp.posted);
        log::write('/');
        log::decimal(msix_info.socket_tx_bp.rejects);
        log::write("; socket_tx_wait stage: ");
        log::decimal(msix_info.socket_tx_wait.stage);
        log::write(", failure: ");
        log::decimal(msix_info.socket_tx_wait.failure);
        log::write(", waits/posted: ");
        log::decimal(msix_info.socket_tx_wait.waits);
        log::write('/');
        log::decimal(msix_info.socket_tx_wait.posted);
        log::write("; irq_rx stage: ");
        log::decimal(msix_info.irq_rx.stage);
        log::write(", failure: ");
        log::decimal(msix_info.irq_rx.failure);
        log::write(", delivered/echoes/recycled: ");
        log::decimal(msix_info.irq_rx.delivered);
        log::write('/');
        log::decimal(msix_info.irq_rx.echoes);
        log::write('/');
        log::decimal(msix_info.irq_rx.recycled);
        log::write("; rx_submit stage: ");
        log::decimal(msix_info.rx_submit.stage);
        log::write(", failure: ");
        log::decimal(msix_info.rx_submit.failure);
        log::write(", submitted: ");
        log::decimal(msix_info.rx_submit.submitted);
        log::write('\n');
        for (const e1000e::VectorResult& result : msix_info.vectors) {
            log::write("[debug] entry ");
            log::decimal(result.table_entry);
            log::write(", vector: ");
            log::decimal(result.idt_vector);
            log::write(", calls: ");
            log::decimal(result.calls);
            log::write(", cross: ");
            log::decimal(result.cross_calls);
            log::write(", errors: ");
            log::decimal(result.errors);
            log::write(", cause: ");
            log::hex(result.last_cause);
            log::write(", error cause: ");
            log::hex(result.error_cause);
            log::write('\n');
        }
    }
    if (!msix_passed || irq::active_count() != 0 ||
        irq::unhandled_count() != 0) {
        log::panic("e1000e MSI-X/packet/protocol/UDP-ring delivery or cleanup failed");
    }
    log::write("[ok] e1000e MSI-X entries: ");
    log::decimal(msix_info.table_entries);
    log::write(", table BAR: ");
    log::hex(msix_info.msix_physical);
    log::write(", per-vector PBA: ");
    log::decimal(msix_info.per_vector_pba_seen);
    log::write(", function PBA: ");
    log::decimal(msix_info.function_pba_seen);
    log::write('\n');
    for (const e1000e::VectorResult& result : msix_info.vectors) {
        log::write("[ok] MSI-X entry ");
        log::decimal(result.table_entry);
        log::write(" -> IDT vector ");
        log::decimal(result.idt_vector);
        log::write(", matched: ");
        log::decimal(result.matched_calls);
        log::write(", empty: ");
        log::decimal(result.empty_calls);
        log::write(", cross: ");
        log::decimal(result.cross_calls);
        log::write(", cause: ");
        log::hex(result.last_cause);
        log::write('\n');
    }
    log::write("[ok] e1000e packet RX/TX frames: ");
    log::decimal(msix_info.packet.rx_frames);
    log::write('/');
    log::decimal(msix_info.packet.tx_frames);
    log::write(", bytes: ");
    log::decimal(msix_info.packet.rx_length);
    log::write('/');
    log::decimal(msix_info.packet.tx_length);
    log::write(", heads: ");
    log::decimal(msix_info.packet.rx_head);
    log::write('/');
    log::decimal(msix_info.packet.tx_head);
    log::write(", IRQs: ");
    log::decimal(msix_info.packet.rx_interrupts);
    log::write('/');
    log::decimal(msix_info.packet.tx_interrupts);
    log::write('\n');
    log::write("[ok] e1000e ARP request/reply: ");
    log::decimal(msix_info.protocol.arp_requests);
    log::write('/');
    log::decimal(msix_info.protocol.arp_replies);
    log::write(", bytes: ");
    log::decimal(msix_info.protocol.arp_rx_bytes);
    log::write('/');
    log::decimal(msix_info.protocol.arp_tx_bytes);
    log::write('\n');
    log::write("[ok] e1000e ICMP echo request/reply: ");
    log::decimal(msix_info.protocol.icmp_requests);
    log::write('/');
    log::decimal(msix_info.protocol.icmp_replies);
    log::write(", bytes: ");
    log::decimal(msix_info.protocol.icmp_rx_bytes);
    log::write('/');
    log::decimal(msix_info.protocol.icmp_tx_bytes);
    log::write(", heads: ");
    log::decimal(msix_info.protocol.rx_head);
    log::write('/');
    log::decimal(msix_info.protocol.tx_head);
    log::write(", IRQs: ");
    log::decimal(msix_info.protocol.rx_interrupts);
    log::write('/');
    log::decimal(msix_info.protocol.tx_interrupts);
    log::write('\n');
    log::write("[ok] e1000e UDP Echo request/reply: ");
    log::decimal(msix_info.udp.requests);
    log::write('/');
    log::decimal(msix_info.udp.replies);
    log::write(", payload bytes: ");
    log::decimal(msix_info.udp.payload_bytes);
    log::write(", frame RX/TX bytes: ");
    log::decimal(msix_info.udp.rx_bytes);
    log::write('/');
    log::decimal(msix_info.udp.tx_bytes);
    // M30: Echo TX may complete from TXQ0 demux; hardware RX IRQ count is not
    // 1:1. Frozen evidence uses request/reply + IRQ deliver/echo line.
    log::write('\n');
    log::write("[ok] e1000e UDP ring heads RX/TX: ");
    log::decimal(msix_info.udp.rx_head);
    log::write('/');
    log::decimal(msix_info.udp.tx_head);
    log::write(", tails RX/TX: ");
    log::decimal(msix_info.udp.rx_tail);
    log::write('/');
    log::decimal(msix_info.udp.tx_tail);
    log::write(", wraps RXh/TXh/RXt/TXt: ");
    log::decimal(msix_info.udp.rx_head_wraps);
    log::write('/');
    log::decimal(msix_info.udp.tx_head_wraps);
    log::write('/');
    log::decimal(msix_info.udp.rx_tail_wraps);
    log::write('/');
    log::decimal(msix_info.udp.tx_tail_wraps);
    log::write(", recycled RX buffers: ");
    log::decimal(msix_info.udp.recycled_buffers);
    log::write('\n');
    log::write("[ok] e1000e UDP socket demux deliver/recv: ");
    log::decimal(msix_info.udp.socket_delivered);
    log::write('/');
    log::decimal(msix_info.udp.socket_received);
    log::write('\n');
    log::write("[ok] e1000e UDP stress request/reply: ");
    log::decimal(msix_info.stress.requests);
    log::write('/');
    log::decimal(msix_info.stress.replies);
    log::write(", payload bytes: ");
    log::decimal(msix_info.stress.payload_bytes);
    log::write(", frame RX/TX bytes: ");
    log::decimal(msix_info.stress.rx_bytes);
    log::write('/');
    log::decimal(msix_info.stress.tx_bytes);
    log::write('\n');
    log::write("[ok] e1000e UDP stress ring heads RX/TX: ");
    log::decimal(msix_info.stress.rx_head);
    log::write('/');
    log::decimal(msix_info.stress.tx_head);
    log::write(", tails RX/TX: ");
    log::decimal(msix_info.stress.rx_tail);
    log::write('/');
    log::decimal(msix_info.stress.tx_tail);
    log::write(", wraps RXh/TXh/RXt/TXt: ");
    log::decimal(msix_info.stress.rx_head_wraps);
    log::write('/');
    log::decimal(msix_info.stress.tx_head_wraps);
    log::write('/');
    log::decimal(msix_info.stress.rx_tail_wraps);
    log::write('/');
    log::decimal(msix_info.stress.tx_tail_wraps);
    log::write(", recycled RX buffers: ");
    log::decimal(msix_info.stress.recycled_buffers);
    log::write('\n');
    log::write("[ok] e1000e UDP stress socket demux deliver/recv: ");
    log::decimal(msix_info.stress.socket_delivered);
    log::write('/');
    log::decimal(msix_info.stress.socket_received);
    log::write('\n');
    log::write("[ok] e1000e UDP socket TX submit: ");
    log::decimal(msix_info.socket_tx.frames);
    log::write('/');
    log::decimal(msix_info.socket_tx.tx_interrupts);
    log::write(", bytes: ");
    log::decimal(msix_info.socket_tx.bytes);
    log::write(", heads/tails: ");
    log::decimal(msix_info.socket_tx.tx_head);
    log::write('/');
    log::decimal(msix_info.socket_tx.tx_tail);
    log::write('\n');
    // Frozen evidence: drain is TDH==TDT (TX IRQs may coalesce; count is not 1:1).
    log::write("[ok] e1000e UDP socket TX backpressure: posted: ");
    log::decimal(msix_info.socket_tx_bp.posted);
    log::write(", rejects: ");
    log::decimal(msix_info.socket_tx_bp.rejects);
    log::write(", drained: 1, heads/tails: ");
    log::decimal(msix_info.socket_tx_bp.tx_head);
    log::write('/');
    log::decimal(msix_info.socket_tx_bp.tx_tail);
    log::write('\n');
    log::write("[ok] e1000e UDP socket TX wait: waits: ");
    log::decimal(msix_info.socket_tx_wait.waits);
    log::write(", posted: ");
    log::decimal(msix_info.socket_tx_wait.posted);
    log::write(", drained: 1, events: ");
    log::decimal(msix_info.socket_tx_wait.events);
    log::write(", heads/tails: ");
    log::decimal(msix_info.socket_tx_wait.tx_head);
    log::write('/');
    log::decimal(msix_info.socket_tx_wait.tx_tail);
    log::write('\n');
    log::write("[ok] e1000e UDP IRQ RX demux deliver/echo/recycle: ");
    log::decimal(msix_info.irq_rx.delivered);
    log::write('/');
    log::decimal(msix_info.irq_rx.echoes);
    log::write('/');
    log::decimal(msix_info.irq_rx.recycled);
    log::write('\n');
    log::write("[ok] e1000e UDP IRQ RX submit: ");
    log::decimal(msix_info.rx_submit.submitted);
    log::write('\n');

    runtime_test::run();

    if (!smp::verify_parked_processors()) {
        log::write("[debug] parked SMP recheck stage: ");
        log::decimal(smp_info.stage);
        log::write(", active CPU: ");
        log::decimal(smp_info.active_cpu);
        log::write('\n');
        log::panic("parked AP IPI recheck failed");
    }
    log::write("[ok] parked AP recheck complete; total fixed IPIs: ");
    log::decimal(smp_info.total_ipi_calls);
    log::write('\n');

    log::line("CXC_OS_SELF_TEST_PASS");
    log::line("[kernel] milestone 13 complete; halting CPU");
    cpu::halt();
}

[[noreturn]] void kernel_stage2(const void* argument) {
    if (argument != &boot_memory_map) {
        log::panic("invalid stage-2 boot argument");
    }
    log::line("[ok] switched to kernel-owned 64 KiB stack");

    if (!vmm::initialize()) {
        log::panic("virtual memory initialization failed");
    }
    log::write("[ok] transition PML4: ");
    log::hex(vmm::root_physical_address());
    log::write(", direct-map 2 MiB pages: ");
    log::decimal(vmm::mapped_2m_pages());
    log::write(", 4 KiB pages: ");
    log::decimal(vmm::mapped_4k_pages());
    log::write('\n');

    if (!vmm::prepare_higher_half(firmware_image_base, firmware_image_size,
                                  kernel_stack_physical)) {
        log::panic("higher-half preparation failed");
    }
    log::write("[ok] higher-half image pages: ");
    log::decimal(vmm::high_image_pages());
    log::write(", total page-table pages: ");
    log::decimal(vmm::page_table_pages());
    log::write('\n');

    const uint64_t entry_rva =
        reinterpret_cast<uint64_t>(&kernel_high_entry) - firmware_image_base;
    if (entry_rva >= firmware_image_size) {
        log::panic("higher-half entry is outside the loaded image");
    }
    const uint64_t high_entry = vmm::kernel_base + entry_rva;

    // Copy the position-independent AP startup code while the firmware's low
    // identity mappings still exist. Runtime code later maps it RX and its
    // following parameter page RW/NX in the shared kernel root.
    if (!smp::prepare_trampoline(smp_trampoline_physical)) {
        log::panic("SMP trampoline preparation failed");
    }

    // The final copy must capture every PMM/page-table allocation. No further
    // allocation or state-changing preparation is allowed after this point.
    boot_memory_map.descriptors = nullptr;
    boot_memory_map.size = 0;
    if (!vmm::finalize_higher_half(firmware_image_base)) {
        log::panic("higher-half PE relocation failed");
    }

    using Entry = void (*)(const void*);
    switch_stack_and_call(vmm::higher_half_stack_top,
                          reinterpret_cast<Entry>(high_entry), nullptr);
}

[[noreturn]] void kernel_main() {
    log::line("[boot] UEFI Boot Services exited; serial-only mode");

    cpu::initialize_gdt();
    cpu::initialize_idt();
    log::line("[ok] transition GDT/IDT loaded");

    // ACPI tables are still reachable through firmware's initial page tables.
    // discover_madt copies all required fields before those mappings vanish.
    if (!acpi::discover_madt(acpi_rsdp_address)) {
        log::panic("ACPI RSDP/XSDT/MADT discovery failed");
    }
    log::write("[ok] MADT processors: ");
    log::decimal(acpi::madt().processor_count);
    log::write(", IOAPICs: ");
    log::decimal(acpi::madt().ioapic_count);
    log::write(", IRQ overrides: ");
    log::decimal(acpi::madt().override_count);
    log::write('\n');

    if (!pmm::initialize(boot_memory_map)) {
        log::panic("physical memory manager initialization failed");
    }
    log::write("[ok] inline-extent PMM pages: ");
    log::decimal(pmm::total_pages());
    log::write(", initial extents: ");
    log::decimal(pmm::extent_count());
    log::write('\n');

    kernel_stack_physical = pmm::allocate_pages(vmm::kernel_stack_pages);
    if (kernel_stack_physical == 0) {
        log::panic("cannot allocate kernel stack");
    }
    const uint64_t stack_top =
        kernel_stack_physical + vmm::kernel_stack_pages * pmm::page_size;
    switch_stack_and_call(stack_top, kernel_stage2, &boot_memory_map);
}

}  // namespace
}  // namespace cxc

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle,
                                       EFI_SYSTEM_TABLE* system_table) {
    cxc::log::initialize(system_table);
    cxc::log::line("CXC/OS 0.35.0 - third midfield; A1 userspace next");

    EFI_STATUS status = cxc::uefi::loaded_image_info(
        image_handle, system_table, cxc::firmware_image_base,
        cxc::firmware_image_size);
    if (efi_error(status)) {
        cxc::log::line("[boot] LoadedImage protocol failed");
        return status;
    }

    status = cxc::uefi::find_acpi_rsdp(system_table, cxc::acpi_rsdp_address);
    if (efi_error(status)) {
        cxc::log::line("[boot] ACPI RSDP configuration table not found");
        return status;
    }

    status = cxc::uefi::allocate_low_pages(
        system_table, 2, 0x00000000000fffff,
        cxc::smp_trampoline_physical);
    if (efi_error(status)) {
        cxc::log::line("[boot] cannot allocate sub-1MiB SMP trampoline pages");
        return status;
    }

    status = cxc::uefi::exit_boot_services(
        image_handle, system_table, cxc::boot_memory_map);
    if (efi_error(status)) {
        cxc::log::write("[boot] ExitBootServices failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }

    cxc::kernel_main();
}
