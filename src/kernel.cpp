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
#include <cxc/user.hpp>
#include <cxc/vmm.hpp>

extern "C" void switch_stack_and_call(
    uint64_t stack_top, void (*entry)(const void*), const void* argument)
    __attribute__((noreturn));
extern "C" void* memcpy(void*, const void*, size_t);

namespace cxc {
namespace {

uefi::BootMemoryMap boot_memory_map;
uint64_t firmware_image_base = 0;
uint64_t firmware_image_size = 0;
uint64_t kernel_stack_physical = 0;
uint64_t acpi_rsdp_address = 0;
uint64_t smp_trampoline_physical = 0;
uint64_t ext_elf_physical = 0;
uint64_t ext_elf_size = 0;
uint64_t cross_elf_physical = 0;
uint64_t cross_elf_size = 0;
uint64_t lseek_elf_physical = 0;
uint64_t lseek_elf_size = 0;
uint64_t fstat_elf_physical = 0;
uint64_t fstat_elf_size = 0;
uint64_t stat_elf_physical = 0;
uint64_t stat_elf_size = 0;
uint64_t getdents_elf_physical = 0;
uint64_t getdents_elf_size = 0;
uint64_t arch_prctl_elf_physical = 0;
uint64_t arch_prctl_elf_size = 0;
uint64_t libc_elf_physical = 0;
uint64_t libc_elf_size = 0;

bool relocate_esp_elf_to_pmm(uint64_t& physical, uint64_t size,
                             const char* label) {
    if (physical == 0 || size == 0) {
        return false;
    }
    const uint64_t pages = (size + pmm::page_size - 1) / pmm::page_size;
    const uint64_t dest = pmm::allocate_pages(pages);
    if (dest == 0) {
        return false;
    }
    memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(dest)),
           reinterpret_cast<const void*>(static_cast<uintptr_t>(physical)),
           static_cast<size_t>(size));
    physical = dest;
    log::write("[ok] ");
    log::write(label);
    log::write(" relocated to PMM pages: ");
    log::decimal(pages);
    log::write('\n');
    return true;
}

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

    vmm::UserAsProbeResult user_as{};
    if (!vmm::run_user_as_probe(user_as)) {
        log::write("[debug] user AS probe stage: ");
        log::decimal(user_as.stage);
        log::write(", failure: ");
        log::decimal(user_as.failure);
        log::write('\n');
        log::panic("M36 user address-space probe failed");
    }
    log::write("[ok] user AS probe: cr3_switch: ");
    log::decimal(user_as.cr3_switch);
    log::write(", write: ");
    log::decimal(user_as.write_ok);
    log::write(", read: ");
    log::decimal(user_as.read_ok);
    log::write(", user_bit: ");
    log::decimal(user_as.user_bit);
    log::write(", va: ");
    log::hex(vmm::user_probe_base);
    log::write('\n');

    vmm::Ring3ProbeResult ring3{};
    if (!vmm::run_ring3_probe(ring3)) {
        log::write("[debug] ring3 probe stage: ");
        log::decimal(ring3.stage);
        log::write(", failure: ");
        log::decimal(ring3.failure);
        log::write('\n');
        log::panic("M37 ring-3 probe failed");
    }
    log::write("[ok] ring3 probe: enter: ");
    log::decimal(ring3.enter);
    log::write(", int80: ");
    log::decimal(ring3.int80);
    log::write(", cpl: ");
    log::decimal(ring3.cpl);
    log::write(", rsp0: ");
    log::decimal(ring3.rsp0);
    log::write('\n');

    user::ElfProbeResult elf{};
    if (!user::run_elf_probe(elf)) {
        log::write("[debug] user ELF probe stage: ");
        log::decimal(elf.stage);
        log::write(", failure: ");
        log::decimal(elf.failure);
        log::write('\n');
        log::panic("M38 user ELF probe failed");
    }
    log::write("[ok] user ELF: loaded: ");
    log::decimal(elf.loaded);
    log::write(", enter: ");
    log::decimal(elf.entered);
    log::write(", exit: ");
    log::decimal(elf.exited);
    log::write(", pass: ");
    log::decimal(elf.wrote_pass);
    log::write('\n');

    user::FsProbeResult fs{};
    if (!user::run_fs_probe(fs)) {
        log::write("[debug] user FS probe stage: ");
        log::decimal(fs.stage);
        log::write(", failure: ");
        log::decimal(fs.failure);
        log::write('\n');
        log::panic("M39 user FS probe failed");
    }
    log::write("[ok] user FS: open: ");
    log::decimal(fs.opens);
    log::write(", read: ");
    log::decimal(fs.reads);
    log::write(", write: ");
    log::decimal(fs.writes);
    log::write(", close: ");
    log::decimal(fs.closes);
    log::write(", pass: ");
    log::decimal(fs.wrote_pass);
    log::write('\n');

    user::BrkProbeResult brk{};
    if (!user::run_brk_probe(brk)) {
        log::write("[debug] user brk probe stage: ");
        log::decimal(brk.stage);
        log::write(", failure: ");
        log::decimal(brk.failure);
        log::write('\n');
        log::panic("M40 user brk probe failed");
    }
    log::write("[ok] user brk: grow: ");
    log::decimal(brk.grows);
    log::write(", pages: ");
    log::decimal(brk.pages);
    log::write(", pass: ");
    log::decimal(brk.wrote_pass);
    log::write('\n');

    user::MmapProbeResult mmap{};
    if (!user::run_mmap_probe(mmap)) {
        log::write("[debug] user mmap probe stage: ");
        log::decimal(mmap.stage);
        log::write(", failure: ");
        log::decimal(mmap.failure);
        log::write('\n');
        log::panic("M41 user mmap probe failed");
    }
    log::write("[ok] user mmap: calls: ");
    log::decimal(mmap.calls);
    log::write(", pages: ");
    log::decimal(mmap.pages);
    log::write(", pass: ");
    log::decimal(mmap.wrote_pass);
    log::write('\n');

    user::DirProbeResult dir{};
    if (!user::run_dir_probe(dir)) {
        log::write("[debug] user dir probe stage: ");
        log::decimal(dir.stage);
        log::write(", failure: ");
        log::decimal(dir.failure);
        log::write('\n');
        log::panic("M42 user dir probe failed");
    }
    log::write("[ok] user dir: mkdir: ");
    log::decimal(dir.mkdirs);
    log::write(", open: ");
    log::decimal(dir.opens);
    log::write(", dirs: ");
    log::decimal(dir.dirs);
    log::write(", pass: ");
    log::decimal(dir.wrote_pass);
    log::write('\n');

    user::MunmapProbeResult munmap{};
    if (!user::run_munmap_probe(munmap)) {
        log::write("[debug] user munmap probe stage: ");
        log::decimal(munmap.stage);
        log::write(", failure: ");
        log::decimal(munmap.failure);
        log::write('\n');
        log::panic("M43 user munmap probe failed");
    }
    log::write("[ok] user munmap: mmap: ");
    log::decimal(munmap.mmap_calls);
    log::write(", munmap: ");
    log::decimal(munmap.munmap_calls);
    log::write(", pass: ");
    log::decimal(munmap.wrote_pass);
    log::write('\n');

    user::ExtElfProbeResult ext{};
    const auto* ext_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + ext_elf_physical);
    if (ext_elf_physical == 0 || ext_elf_size == 0 ||
        !user::run_ext_elf_probe(ext_image, static_cast<size_t>(ext_elf_size),
                                 ext)) {
        log::write("[debug] user ext ELF probe stage: ");
        log::decimal(ext.stage);
        log::write(", failure: ");
        log::decimal(ext.failure);
        log::write(", size: ");
        log::decimal(ext_elf_size);
        log::write('\n');
        log::panic("M45 user ext ELF probe failed");
    }
    log::write("[ok] user ext ELF: loaded: ");
    log::decimal(ext.loaded);
    log::write(", enter: ");
    log::decimal(ext.entered);
    log::write(", exit: ");
    log::decimal(ext.exited);
    log::write(", pass: ");
    log::decimal(ext.wrote_pass);
    log::write('\n');

    user::ExtElfProbeResult cross{};
    const auto* cross_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + cross_elf_physical);
    if (cross_elf_physical == 0 || cross_elf_size == 0 ||
        !user::run_cross_elf_probe(cross_image,
                                   static_cast<size_t>(cross_elf_size),
                                   cross)) {
        log::write("[debug] user cross ELF probe stage: ");
        log::decimal(cross.stage);
        log::write(", failure: ");
        log::decimal(cross.failure);
        log::write(", size: ");
        log::decimal(cross_elf_size);
        log::write('\n');
        log::panic("M46 user cross ELF probe failed");
    }
    log::write("[ok] user cross ELF: loaded: ");
    log::decimal(cross.loaded);
    log::write(", enter: ");
    log::decimal(cross.entered);
    log::write(", exit: ");
    log::decimal(cross.exited);
    log::write(", pass: ");
    log::decimal(cross.wrote_pass);
    log::write('\n');

    user::LseekProbeResult lseek{};
    const auto* lseek_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + lseek_elf_physical);
    if (lseek_elf_physical == 0 || lseek_elf_size == 0 ||
        !user::run_lseek_probe(lseek_image, static_cast<size_t>(lseek_elf_size),
                               lseek)) {
        log::write("[debug] user lseek probe stage: ");
        log::decimal(lseek.stage);
        log::write(", failure: ");
        log::decimal(lseek.failure);
        log::write(", lseeks: ");
        log::decimal(lseek.lseek_calls);
        log::write('\n');
        log::panic("M47 user lseek probe failed");
    }
    log::write("[ok] user lseek: calls: ");
    log::decimal(lseek.lseek_calls);
    log::write(", pass: ");
    log::decimal(lseek.wrote_pass);
    log::write('\n');

    user::FstatProbeResult fstat{};
    const auto* fstat_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + fstat_elf_physical);
    if (fstat_elf_physical == 0 || fstat_elf_size == 0 ||
        !user::run_fstat_probe(fstat_image, static_cast<size_t>(fstat_elf_size),
                               fstat)) {
        log::write("[debug] user fstat probe stage: ");
        log::decimal(fstat.stage);
        log::write(", failure: ");
        log::decimal(fstat.failure);
        log::write(", fstats: ");
        log::decimal(fstat.fstat_calls);
        log::write('\n');
        log::panic("M48 user fstat probe failed");
    }
    log::write("[ok] user fstat: calls: ");
    log::decimal(fstat.fstat_calls);
    log::write(", pass: ");
    log::decimal(fstat.wrote_pass);
    log::write('\n');

    user::StatProbeResult path_stat{};
    const auto* path_stat_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + stat_elf_physical);
    if (stat_elf_physical == 0 || stat_elf_size == 0 ||
        !user::run_stat_probe(path_stat_image,
                              static_cast<size_t>(stat_elf_size), path_stat)) {
        log::write("[debug] user stat probe stage: ");
        log::decimal(path_stat.stage);
        log::write(", failure: ");
        log::decimal(path_stat.failure);
        log::write(", stats: ");
        log::decimal(path_stat.stat_calls);
        log::write('\n');
        log::panic("M49 user stat probe failed");
    }
    log::write("[ok] user stat: calls: ");
    log::decimal(path_stat.stat_calls);
    log::write(", pass: ");
    log::decimal(path_stat.wrote_pass);
    log::write('\n');

    user::GetdentsProbeResult dents{};
    const auto* dents_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + getdents_elf_physical);
    if (getdents_elf_physical == 0 || getdents_elf_size == 0 ||
        !user::run_getdents_probe(dents_image,
                                  static_cast<size_t>(getdents_elf_size),
                                  dents)) {
        log::write("[debug] user getdents probe stage: ");
        log::decimal(dents.stage);
        log::write(", failure: ");
        log::decimal(dents.failure);
        log::write(", getdents: ");
        log::decimal(dents.getdents_calls);
        log::write('\n');
        log::panic("M50 user getdents probe failed");
    }
    log::write("[ok] user getdents: calls: ");
    log::decimal(dents.getdents_calls);
    log::write(", pass: ");
    log::decimal(dents.wrote_pass);
    log::write('\n');

    user::ArchPrctlProbeResult prctl{};
    const auto* prctl_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + arch_prctl_elf_physical);
    if (arch_prctl_elf_physical == 0 || arch_prctl_elf_size == 0 ||
        !user::run_arch_prctl_probe(prctl_image,
                                    static_cast<size_t>(arch_prctl_elf_size),
                                    prctl)) {
        log::write("[debug] user arch_prctl probe stage: ");
        log::decimal(prctl.stage);
        log::write(", failure: ");
        log::decimal(prctl.failure);
        log::write(", arch_prctl: ");
        log::decimal(prctl.arch_prctl_calls);
        log::write('\n');
        log::panic("M51 user arch_prctl probe failed");
    }
    log::write("[ok] user arch_prctl: calls: ");
    log::decimal(prctl.arch_prctl_calls);
    log::write(", pass: ");
    log::decimal(prctl.wrote_pass);
    log::write('\n');

    user::LibcProbeResult libc{};
    const auto* libc_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_elf_physical);
    if (libc_elf_physical == 0 || libc_elf_size == 0 ||
        !user::run_libc_probe(libc_image, static_cast<size_t>(libc_elf_size),
                              libc)) {
        log::write("[debug] user libc probe stage: ");
        log::decimal(libc.stage);
        log::write(", failure: ");
        log::decimal(libc.failure);
        log::write(", write: ");
        log::decimal(libc.write_calls);
        log::write('\n');
        log::panic("M52 user libc probe failed");
    }
    log::write("[ok] user libc: write: ");
    log::decimal(libc.write_calls);
    log::write(", pass: ");
    log::decimal(libc.wrote_pass);
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

    // EfiLoaderData pages are not covered by the ConventionalMemory direct
    // map. Copy ESP ELFs into PMM pages while firmware identity maps remain
    // (before higher-half drops the low half).
    if (!relocate_esp_elf_to_pmm(ext_elf_physical, ext_elf_size,
                                 "ESP EXT.ELF")) {
        log::panic("cannot relocate ESP EXT.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(cross_elf_physical, cross_elf_size,
                                 "ESP CROSS.ELF")) {
        log::panic("cannot relocate ESP CROSS.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(lseek_elf_physical, lseek_elf_size,
                                 "ESP LSEEK.ELF")) {
        log::panic("cannot relocate ESP LSEEK.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(fstat_elf_physical, fstat_elf_size,
                                 "ESP FSTAT.ELF")) {
        log::panic("cannot relocate ESP FSTAT.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(stat_elf_physical, stat_elf_size,
                                 "ESP STAT.ELF")) {
        log::panic("cannot relocate ESP STAT.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(getdents_elf_physical, getdents_elf_size,
                                 "ESP DENTS.ELF")) {
        log::panic("cannot relocate ESP DENTS.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(arch_prctl_elf_physical, arch_prctl_elf_size,
                                 "ESP PRCTL.ELF")) {
        log::panic("cannot relocate ESP PRCTL.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_elf_physical, libc_elf_size,
                                 "ESP LIBC.ELF")) {
        log::panic("cannot relocate ESP LIBC.ELF into PMM");
    }

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
    cxc::log::line("CXC/OS 0.53.0 - mini-libc write/_exit (A2/M52)");

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

    // Path is UTF-16: \EFI\BOOT\EXT.ELF (M45 non-embedded user image).
    static CHAR16 ext_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'E',  u'X', u'T', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, ext_elf_path,
                                      64 * 1024, cxc::ext_elf_physical,
                                      cxc::ext_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP EXT.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP EXT.ELF bytes: ");
    cxc::log::decimal(cxc::ext_elf_size);
    cxc::log::write('\n');

    static CHAR16 cross_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'C',  u'R', u'O', u'S', u'S',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, cross_elf_path,
                                      64 * 1024, cxc::cross_elf_physical,
                                      cxc::cross_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP CROSS.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP CROSS.ELF bytes: ");
    cxc::log::decimal(cxc::cross_elf_size);
    cxc::log::write('\n');

    static CHAR16 lseek_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'S', u'E', u'E', u'K',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, lseek_elf_path,
                                      64 * 1024, cxc::lseek_elf_physical,
                                      cxc::lseek_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LSEEK.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LSEEK.ELF bytes: ");
    cxc::log::decimal(cxc::lseek_elf_size);
    cxc::log::write('\n');

    static CHAR16 fstat_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'F',  u'S', u'T', u'A', u'T',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, fstat_elf_path,
                                      64 * 1024, cxc::fstat_elf_physical,
                                      cxc::fstat_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP FSTAT.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP FSTAT.ELF bytes: ");
    cxc::log::decimal(cxc::fstat_elf_size);
    cxc::log::write('\n');

    static CHAR16 stat_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'S',  u'T', u'A', u'T', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, stat_elf_path,
                                      64 * 1024, cxc::stat_elf_physical,
                                      cxc::stat_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP STAT.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP STAT.ELF bytes: ");
    cxc::log::decimal(cxc::stat_elf_size);
    cxc::log::write('\n');

    static CHAR16 getdents_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'D',  u'E', u'N', u'T', u'S',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, getdents_elf_path, 64 * 1024,
        cxc::getdents_elf_physical, cxc::getdents_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP DENTS.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP DENTS.ELF bytes: ");
    cxc::log::decimal(cxc::getdents_elf_size);
    cxc::log::write('\n');

    static CHAR16 arch_prctl_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'P',  u'R', u'C', u'T', u'L', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, arch_prctl_elf_path, 64 * 1024,
        cxc::arch_prctl_elf_physical, cxc::arch_prctl_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP PRCTL.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP PRCTL.ELF bytes: ");
    cxc::log::decimal(cxc::arch_prctl_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'C', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, libc_elf_path,
                                      64 * 1024, cxc::libc_elf_physical,
                                      cxc::libc_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBC.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBC.ELF bytes: ");
    cxc::log::decimal(cxc::libc_elf_size);
    cxc::log::write('\n');

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
