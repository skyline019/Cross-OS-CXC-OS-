#include <cxc/acpi.hpp>
#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/e1000e.hpp>
#include <cxc/edu.hpp>
#include <cxc/fs.hpp>
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
#include <cxc/cxcfs.hpp>
#include <cxc/virtio_blk.hpp>
#include <cxc/vmm.hpp>

extern "C" void switch_stack_and_call(
    uint64_t stack_top, void (*entry)(const void*), const void* argument)
    __attribute__((noreturn));
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

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
uint64_t libc_fs_elf_physical = 0;
uint64_t libc_fs_elf_size = 0;
uint64_t libc_sbrk_elf_physical = 0;
uint64_t libc_sbrk_elf_size = 0;
uint64_t libc_malloc_elf_physical = 0;
uint64_t libc_malloc_elf_size = 0;
uint64_t libc_errno_elf_physical = 0;
uint64_t libc_errno_elf_size = 0;
uint64_t libc_string_elf_physical = 0;
uint64_t libc_string_elf_size = 0;
uint64_t libc_writev_elf_physical = 0;
uint64_t libc_writev_elf_size = 0;
uint64_t libc_crt_elf_physical = 0;
uint64_t libc_crt_elf_size = 0;
uint64_t libc_uname_elf_physical = 0;
uint64_t libc_uname_elf_size = 0;
uint64_t libc_tid_elf_physical = 0;
uint64_t libc_tid_elf_size = 0;
uint64_t libc_pid_elf_physical = 0;
uint64_t libc_pid_elf_size = 0;
uint64_t musl_write_elf_physical = 0;
uint64_t musl_write_elf_size = 0;
uint64_t musl_auxv_elf_physical = 0;
uint64_t musl_auxv_elf_size = 0;
uint64_t musl_crt_elf_physical = 0;
uint64_t musl_crt_elf_size = 0;
uint64_t musl_malloc_elf_physical = 0;
uint64_t musl_malloc_elf_size = 0;
uint64_t musl_printf_elf_physical = 0;
uint64_t musl_printf_elf_size = 0;
uint64_t musl_fopen_elf_physical = 0;
uint64_t musl_fopen_elf_size = 0;
uint64_t musl_opendir_elf_physical = 0;
uint64_t musl_opendir_elf_size = 0;
uint64_t musl_unlink_elf_physical = 0;
uint64_t musl_unlink_elf_size = 0;
uint64_t musl_blkfs_elf_physical = 0;
uint64_t musl_blkfs_elf_size = 0;
uint64_t musl_blkdir_elf_physical = 0;
uint64_t musl_blkdir_elf_size = 0;
uint64_t disk_exec_stub_elf_physical = 0;
uint64_t disk_exec_stub_elf_size = 0;
uint64_t disk_exec_payload_elf_physical = 0;
uint64_t disk_exec_payload_elf_size = 0;
uint64_t fork_wait_elf_physical = 0;
uint64_t fork_wait_elf_size = 0;
uint64_t pipe_elf_physical = 0;
uint64_t pipe_elf_size = 0;
uint64_t chdir_elf_physical = 0;
uint64_t chdir_elf_size = 0;
uint64_t nest_elf_physical = 0;
uint64_t nest_elf_size = 0;
uint64_t cap_elf_physical = 0;
uint64_t cap_elf_size = 0;
uint64_t load128_stub_elf_physical = 0;
uint64_t load128_stub_elf_size = 0;
uint64_t load128_payload_elf_physical = 0;
uint64_t load128_payload_elf_size = 0;
uint64_t heap_elf_physical = 0;
uint64_t heap_elf_size = 0;
uint64_t time_elf_physical = 0;
uint64_t time_elf_size = 0;
uint64_t onbox_stub_elf_physical = 0;
uint64_t onbox_stub_elf_size = 0;
uint64_t onbox_cc_elf_physical = 0;
uint64_t onbox_cc_elf_size = 0;

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

    user::LibcFsProbeResult libc_fs{};
    const auto* libc_fs_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_fs_elf_physical);
    if (libc_fs_elf_physical == 0 || libc_fs_elf_size == 0 ||
        !user::run_libc_fs_probe(libc_fs_image,
                                 static_cast<size_t>(libc_fs_elf_size),
                                 libc_fs)) {
        log::write("[debug] user libc fs probe stage: ");
        log::decimal(libc_fs.stage);
        log::write(", failure: ");
        log::decimal(libc_fs.failure);
        log::write(", open: ");
        log::decimal(libc_fs.opens);
        log::write('\n');
        log::panic("M53 user libc fs probe failed");
    }
    log::write("[ok] user libc fs: open: ");
    log::decimal(libc_fs.opens);
    log::write(", pass: ");
    log::decimal(libc_fs.wrote_pass);
    log::write('\n');

    user::LibcSbrkProbeResult libc_sbrk{};
    const auto* libc_sbrk_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_sbrk_elf_physical);
    if (libc_sbrk_elf_physical == 0 || libc_sbrk_elf_size == 0 ||
        !user::run_libc_sbrk_probe(libc_sbrk_image,
                                   static_cast<size_t>(libc_sbrk_elf_size),
                                   libc_sbrk)) {
        log::write("[debug] user libc sbrk probe stage: ");
        log::decimal(libc_sbrk.stage);
        log::write(", failure: ");
        log::decimal(libc_sbrk.failure);
        log::write(", grow: ");
        log::decimal(libc_sbrk.grows);
        log::write('\n');
        log::panic("M55 user libc sbrk probe failed");
    }
    log::write("[ok] user libc sbrk: grow: ");
    log::decimal(libc_sbrk.grows);
    log::write(", pass: ");
    log::decimal(libc_sbrk.wrote_pass);
    log::write('\n');

    user::LibcMallocProbeResult libc_malloc{};
    const auto* libc_malloc_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_malloc_elf_physical);
    if (libc_malloc_elf_physical == 0 || libc_malloc_elf_size == 0 ||
        !user::run_libc_malloc_probe(
            libc_malloc_image, static_cast<size_t>(libc_malloc_elf_size),
            libc_malloc)) {
        log::write("[debug] user libc malloc probe stage: ");
        log::decimal(libc_malloc.stage);
        log::write(", failure: ");
        log::decimal(libc_malloc.failure);
        log::write(", grow: ");
        log::decimal(libc_malloc.grows);
        log::write('\n');
        log::panic("M56 user libc malloc probe failed");
    }
    log::write("[ok] user libc malloc: grow: ");
    log::decimal(libc_malloc.grows);
    log::write(", pass: ");
    log::decimal(libc_malloc.wrote_pass);
    log::write('\n');

    user::LibcErrnoProbeResult libc_errno{};
    const auto* libc_errno_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_errno_elf_physical);
    if (libc_errno_elf_physical == 0 || libc_errno_elf_size == 0 ||
        !user::run_libc_errno_probe(libc_errno_image,
                                    static_cast<size_t>(libc_errno_elf_size),
                                    libc_errno)) {
        log::write("[debug] user libc errno probe stage: ");
        log::decimal(libc_errno.stage);
        log::write(", failure: ");
        log::decimal(libc_errno.failure);
        log::write('\n');
        log::panic("M57 user libc errno probe failed");
    }
    log::write("[ok] user libc errno: pass: ");
    log::decimal(libc_errno.wrote_pass);
    log::write('\n');

    user::LibcStringProbeResult libc_string{};
    const auto* libc_string_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_string_elf_physical);
    if (libc_string_elf_physical == 0 || libc_string_elf_size == 0 ||
        !user::run_libc_string_probe(libc_string_image,
                                     static_cast<size_t>(libc_string_elf_size),
                                     libc_string)) {
        log::write("[debug] user libc string probe stage: ");
        log::decimal(libc_string.stage);
        log::write(", failure: ");
        log::decimal(libc_string.failure);
        log::write('\n');
        log::panic("M58 user libc string probe failed");
    }
    log::write("[ok] user libc string: pass: ");
    log::decimal(libc_string.wrote_pass);
    log::write('\n');

    user::LibcWritevProbeResult libc_writev{};
    const auto* libc_writev_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_writev_elf_physical);
    if (libc_writev_elf_physical == 0 || libc_writev_elf_size == 0 ||
        !user::run_libc_writev_probe(libc_writev_image,
                                     static_cast<size_t>(libc_writev_elf_size),
                                     libc_writev)) {
        log::write("[debug] user libc writev probe stage: ");
        log::decimal(libc_writev.stage);
        log::write(", failure: ");
        log::decimal(libc_writev.failure);
        log::write('\n');
        log::panic("M59 user libc writev probe failed");
    }
    log::write("[ok] user libc writev: pass: ");
    log::decimal(libc_writev.wrote_pass);
    log::write('\n');

    user::LibcCrtProbeResult libc_crt{};
    const auto* libc_crt_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_crt_elf_physical);
    if (libc_crt_elf_physical == 0 || libc_crt_elf_size == 0 ||
        !user::run_libc_crt_probe(libc_crt_image,
                                  static_cast<size_t>(libc_crt_elf_size),
                                  libc_crt)) {
        log::write("[debug] user libc crt probe stage: ");
        log::decimal(libc_crt.stage);
        log::write(", failure: ");
        log::decimal(libc_crt.failure);
        log::write('\n');
        log::panic("M60 user libc crt probe failed");
    }
    log::write("[ok] user libc crt: pass: ");
    log::decimal(libc_crt.wrote_pass);
    log::write('\n');

    user::LibcUnameProbeResult libc_uname{};
    const auto* libc_uname_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_uname_elf_physical);
    if (libc_uname_elf_physical == 0 || libc_uname_elf_size == 0 ||
        !user::run_libc_uname_probe(libc_uname_image,
                                    static_cast<size_t>(libc_uname_elf_size),
                                    libc_uname)) {
        log::write("[debug] user libc uname probe stage: ");
        log::decimal(libc_uname.stage);
        log::write(", failure: ");
        log::decimal(libc_uname.failure);
        log::write('\n');
        log::panic("M61 user libc uname probe failed");
    }
    log::write("[ok] user libc uname: pass: ");
    log::decimal(libc_uname.wrote_pass);
    log::write('\n');

    user::LibcTidProbeResult libc_tid{};
    const auto* libc_tid_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_tid_elf_physical);
    if (libc_tid_elf_physical == 0 || libc_tid_elf_size == 0 ||
        !user::run_libc_tid_probe(libc_tid_image,
                                  static_cast<size_t>(libc_tid_elf_size),
                                  libc_tid)) {
        log::write("[debug] user libc tid probe stage: ");
        log::decimal(libc_tid.stage);
        log::write(", failure: ");
        log::decimal(libc_tid.failure);
        log::write('\n');
        log::panic("M62 user libc tid probe failed");
    }
    log::write("[ok] user libc tid: pass: ");
    log::decimal(libc_tid.wrote_pass);
    log::write('\n');

    user::LibcPidProbeResult libc_pid{};
    const auto* libc_pid_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + libc_pid_elf_physical);
    if (libc_pid_elf_physical == 0 || libc_pid_elf_size == 0 ||
        !user::run_libc_pid_probe(libc_pid_image,
                                  static_cast<size_t>(libc_pid_elf_size),
                                  libc_pid)) {
        log::write("[debug] user libc pid probe stage: ");
        log::decimal(libc_pid.stage);
        log::write(", failure: ");
        log::decimal(libc_pid.failure);
        log::write('\n');
        log::panic("M63 user libc pid probe failed");
    }
    log::write("[ok] user libc pid: pass: ");
    log::decimal(libc_pid.wrote_pass);
    log::write('\n');

    user::MuslWriteProbeResult musl_write{};
    const auto* musl_write_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_write_elf_physical);
    if (musl_write_elf_physical == 0 || musl_write_elf_size == 0 ||
        !user::run_musl_write_probe(musl_write_image,
                                    static_cast<size_t>(musl_write_elf_size),
                                    musl_write)) {
        log::write("[debug] user musl write probe stage: ");
        log::decimal(musl_write.stage);
        log::write(", failure: ");
        log::decimal(musl_write.failure);
        log::write('\n');
        log::panic("M65 user musl write probe failed");
    }
    log::write("[ok] user musl write: pass: ");
    log::decimal(musl_write.wrote_pass);
    log::write('\n');

    user::MuslAuxvProbeResult musl_auxv{};
    const auto* musl_auxv_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_auxv_elf_physical);
    if (musl_auxv_elf_physical == 0 || musl_auxv_elf_size == 0 ||
        !user::run_musl_auxv_probe(musl_auxv_image,
                                   static_cast<size_t>(musl_auxv_elf_size),
                                   musl_auxv)) {
        log::write("[debug] user musl auxv probe stage: ");
        log::decimal(musl_auxv.stage);
        log::write(", failure: ");
        log::decimal(musl_auxv.failure);
        log::write('\n');
        log::panic("M67 user musl auxv probe failed");
    }
    log::write("[ok] user musl auxv: pass: ");
    log::decimal(musl_auxv.wrote_pass);
    log::write('\n');

    user::MuslCrtProbeResult musl_crt{};
    const auto* musl_crt_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_crt_elf_physical);
    if (musl_crt_elf_physical == 0 || musl_crt_elf_size == 0 ||
        !user::run_musl_crt_probe(musl_crt_image,
                                  static_cast<size_t>(musl_crt_elf_size),
                                  musl_crt)) {
        log::write("[debug] user musl crt probe stage: ");
        log::decimal(musl_crt.stage);
        log::write(", failure: ");
        log::decimal(musl_crt.failure);
        log::write('\n');
        log::panic("M68 user musl crt probe failed");
    }
    log::write("[ok] user musl crt: pass: ");
    log::decimal(musl_crt.wrote_pass);
    log::write('\n');

    user::MuslMallocProbeResult musl_malloc{};
    const auto* musl_malloc_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_malloc_elf_physical);
    if (musl_malloc_elf_physical == 0 || musl_malloc_elf_size == 0 ||
        !user::run_musl_malloc_probe(
            musl_malloc_image, static_cast<size_t>(musl_malloc_elf_size),
            musl_malloc)) {
        log::write("[debug] user musl malloc probe stage: ");
        log::decimal(musl_malloc.stage);
        log::write(", failure: ");
        log::decimal(musl_malloc.failure);
        log::write('\n');
        log::panic("M69 user musl malloc probe failed");
    }
    log::write("[ok] user musl malloc: pass: ");
    log::decimal(musl_malloc.wrote_pass);
    log::write('\n');

    user::MuslPrintfProbeResult musl_printf{};
    const auto* musl_printf_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_printf_elf_physical);
    if (musl_printf_elf_physical == 0 || musl_printf_elf_size == 0 ||
        !user::run_musl_printf_probe(
            musl_printf_image, static_cast<size_t>(musl_printf_elf_size),
            musl_printf)) {
        log::write("[debug] user musl printf probe stage: ");
        log::decimal(musl_printf.stage);
        log::write(", failure: ");
        log::decimal(musl_printf.failure);
        log::write('\n');
        log::panic("M70 user musl printf probe failed");
    }
    log::write("[ok] user musl printf: pass: ");
    log::decimal(musl_printf.wrote_pass);
    log::write('\n');

    user::MuslFopenProbeResult musl_fopen{};
    const auto* musl_fopen_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_fopen_elf_physical);
    if (musl_fopen_elf_physical == 0 || musl_fopen_elf_size == 0 ||
        !user::run_musl_fopen_probe(
            musl_fopen_image, static_cast<size_t>(musl_fopen_elf_size),
            musl_fopen)) {
        log::write("[debug] user musl fopen probe stage: ");
        log::decimal(musl_fopen.stage);
        log::write(", failure: ");
        log::decimal(musl_fopen.failure);
        log::write(", exit: ");
        log::decimal(musl_fopen.exit_code);
        log::write(", pass: ");
        log::decimal(musl_fopen.wrote_pass);
        log::write('\n');
        log::panic("M71 user musl fopen probe failed");
    }
    log::write("[ok] user musl fopen: pass: ");
    log::decimal(musl_fopen.wrote_pass);
    log::write('\n');

    user::MuslOpendirProbeResult musl_opendir{};
    const auto* musl_opendir_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_opendir_elf_physical);
    if (musl_opendir_elf_physical == 0 || musl_opendir_elf_size == 0 ||
        !user::run_musl_opendir_probe(
            musl_opendir_image, static_cast<size_t>(musl_opendir_elf_size),
            musl_opendir)) {
        log::write("[debug] user musl opendir probe stage: ");
        log::decimal(musl_opendir.stage);
        log::write(", failure: ");
        log::decimal(musl_opendir.failure);
        log::write(", exit: ");
        log::decimal(musl_opendir.exit_code);
        log::write(", pass: ");
        log::decimal(musl_opendir.wrote_pass);
        log::write('\n');
        log::panic("M72 user musl opendir probe failed");
    }
    log::write("[ok] user musl opendir: pass: ");
    log::decimal(musl_opendir.wrote_pass);
    log::write('\n');

    user::MuslUnlinkProbeResult musl_unlink{};
    const auto* musl_unlink_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_unlink_elf_physical);
    if (musl_unlink_elf_physical == 0 || musl_unlink_elf_size == 0 ||
        !user::run_musl_unlink_probe(
            musl_unlink_image, static_cast<size_t>(musl_unlink_elf_size),
            musl_unlink)) {
        log::write("[debug] user musl unlink probe stage: ");
        log::decimal(musl_unlink.stage);
        log::write(", failure: ");
        log::decimal(musl_unlink.failure);
        log::write(", exit: ");
        log::decimal(musl_unlink.exit_code);
        log::write(", pass: ");
        log::decimal(musl_unlink.wrote_pass);
        log::write('\n');
        log::panic("M73 user musl unlink probe failed");
    }
    log::write("[ok] user musl unlink: pass: ");
    log::decimal(musl_unlink.wrote_pass);
    log::write('\n');

    virtio_blk::ProbeResult virtio{};
    if (!virtio_blk::run_probe(virtio) || virtio.pass == 0) {
        log::write("[debug] virtio-blk stage: ");
        log::decimal(virtio.stage);
        log::write(", failure: ");
        log::decimal(virtio.failure);
        log::write(", found: ");
        log::decimal(virtio.found);
        log::write(", features: ");
        log::decimal(virtio.features_ok);
        log::write(", queue: ");
        log::decimal(virtio.queue_ok);
        log::write(", read: ");
        log::decimal(virtio.read_ok);
        log::write(", magic: ");
        log::decimal(virtio.magic_ok);
        log::write(", oob: ");
        log::decimal(virtio.oob_rejected);
        log::write('\n');
        log::panic("M75 virtio-blk probe failed");
    }
    log::write("[ok] virtio-blk: pass: ");
    log::decimal(virtio.pass);
    log::write(" capacity: ");
    log::decimal(virtio.capacity_sectors);
    log::write(" oob: ");
    log::decimal(virtio.oob_rejected);
    log::write('\n');
    log::line("VIRTIO_BLK_PASS");

    if (!virtio_blk::run_rw_probe(virtio) || virtio.rw_pass == 0) {
        log::write("[debug] virtio-blk-rw stage: ");
        log::decimal(virtio.stage);
        log::write(", failure: ");
        log::decimal(virtio.failure);
        log::write(", write: ");
        log::decimal(virtio.write_ok);
        log::write(", roundtrip: ");
        log::decimal(virtio.rw_roundtrip_ok);
        log::write(", oob_w: ");
        log::decimal(virtio.oob_write_rejected);
        log::write(", io_fail: ");
        log::decimal(virtio_blk::last_io_failure());
        log::write('\n');
        log::panic("M76 virtio-blk R/W probe failed");
    }
    log::write("[ok] virtio-blk-rw: pass: ");
    log::decimal(virtio.rw_pass);
    log::write(" write: ");
    log::decimal(virtio.write_ok);
    log::write(" roundtrip: ");
    log::decimal(virtio.rw_roundtrip_ok);
    log::write(" oob_w: ");
    log::decimal(virtio.oob_write_rejected);
    log::write(" online: ");
    log::decimal(virtio_blk::is_ready() ? 1u : 0u);
    log::write('\n');
    log::line("VIRTIO_BLK_RW_PASS");

    cxcfs::MountResult cxcfs_probe{};
    if (!cxcfs::run_probe(cxcfs_probe) || cxcfs_probe.pass == 0) {
        log::write("[debug] cxcfs stage: ");
        log::decimal(cxcfs_probe.stage);
        log::write(", failure: ");
        log::decimal(cxcfs_probe.failure);
        log::write(", mounted: ");
        log::decimal(cxcfs_probe.mounted);
        log::write(", persist: ");
        log::decimal(cxcfs_probe.persist_ok);
        log::write(", remount: ");
        log::decimal(cxcfs_probe.remount_ok);
        log::write('\n');
        log::panic("M77 CXCFS probe failed");
    }
    // User-visible path via existing open/read/write (disk/ prefix).
    const int64_t ufd = fs::open_path("disk/user.txt", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
    if (ufd < 0) {
        log::panic("M77 disk/user.txt create failed");
    }
    static const char user_payload[] = "DISKOK";
    if (fs::write_fd(static_cast<uint32_t>(ufd), user_payload,
                     sizeof(user_payload) - 1) !=
        static_cast<int64_t>(sizeof(user_payload) - 1)) {
        log::panic("M77 disk/user.txt write failed");
    }
    if (fs::close_fd(static_cast<uint32_t>(ufd)) != 0) {
        log::panic("M77 disk/user.txt close failed");
    }
    cxcfs::MountResult remount_check = cxcfs_probe;
    if (!cxcfs::remount(remount_check)) {
        log::panic("M77 CXCFS remount after user path failed");
    }
    const int64_t rfd = fs::open_path("disk/user.txt", fs::o_rdonly, 0);
    if (rfd < 0) {
        log::panic("M77 disk/user.txt reopen failed");
    }
    char user_buf[8]{};
    if (fs::read_fd(static_cast<uint32_t>(rfd), user_buf, sizeof(user_buf)) !=
        static_cast<int64_t>(sizeof(user_payload) - 1)) {
        log::panic("M77 disk/user.txt read failed");
    }
    if (fs::close_fd(static_cast<uint32_t>(rfd)) != 0) {
        log::panic("M77 disk/user.txt reclose failed");
    }
    if (user_buf[0] != 'D' || user_buf[1] != 'I' || user_buf[2] != 'S' ||
        user_buf[3] != 'K' || user_buf[4] != 'O' || user_buf[5] != 'K') {
        log::panic("M77 disk/user.txt payload mismatch");
    }

    log::write("[ok] cxcfs: pass: ");
    log::decimal(cxcfs_probe.pass);
    log::write(" persist: ");
    log::decimal(cxcfs_probe.persist_ok);
    log::write(" remount: ");
    log::decimal(remount_check.remount_ok);
    log::write(" formatted: ");
    log::decimal(cxcfs_probe.formatted);
    log::write(" disk_user: 1");
    log::write('\n');
    log::line("CXCFS_PASS");

    user::MuslBlkfsProbeResult musl_blkfs{};
    const auto* musl_blkfs_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_blkfs_elf_physical);
    if (musl_blkfs_elf_physical == 0 || musl_blkfs_elf_size == 0 ||
        !user::run_musl_blkfs_probe(
            musl_blkfs_image, static_cast<size_t>(musl_blkfs_elf_size),
            musl_blkfs)) {
        log::write("[debug] user musl blkfs probe stage: ");
        log::decimal(musl_blkfs.stage);
        log::write(", failure: ");
        log::decimal(musl_blkfs.failure);
        log::write(", exit: ");
        log::decimal(musl_blkfs.exit_code);
        log::write(", pass: ");
        log::decimal(musl_blkfs.wrote_pass);
        log::write('\n');
        log::panic("M78 user musl blkfs probe failed");
    }
    // Persist userspace write across remount.
    cxcfs::MountResult after_user = remount_check;
    if (!cxcfs::remount(after_user)) {
        log::panic("M78 remount after musl blkfs failed");
    }
    const int64_t check_fd = fs::open_path("disk/muslblk.txt", fs::o_rdonly, 0);
    if (check_fd < 0) {
        log::panic("M78 disk/muslblk.txt missing after remount");
    }
    char check_buf[8]{};
    if (fs::read_fd(static_cast<uint32_t>(check_fd), check_buf, 4) != 4 ||
        check_buf[0] != 'M' || check_buf[1] != 'B' || check_buf[2] != 'L' ||
        check_buf[3] != 'K') {
        log::panic("M78 disk/muslblk.txt content mismatch after remount");
    }
    (void)fs::close_fd(static_cast<uint32_t>(check_fd));

    log::write("[ok] user musl blkfs: pass: ");
    log::decimal(musl_blkfs.wrote_pass);
    log::write(" remount: 1");
    log::write('\n');
    log::line("USER_MUSL_BLK_PASS");

    user::MuslBlkdirProbeResult musl_blkdir{};
    const auto* musl_blkdir_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + musl_blkdir_elf_physical);
    if (musl_blkdir_elf_physical == 0 || musl_blkdir_elf_size == 0 ||
        !user::run_musl_blkdir_probe(
            musl_blkdir_image, static_cast<size_t>(musl_blkdir_elf_size),
            musl_blkdir)) {
        log::write("[debug] user musl blkdir probe stage: ");
        log::decimal(musl_blkdir.stage);
        log::write(", failure: ");
        log::decimal(musl_blkdir.failure);
        log::write(", exit: ");
        log::decimal(musl_blkdir.exit_code);
        log::write(", pass: ");
        log::decimal(musl_blkdir.wrote_pass);
        log::write('\n');
        log::panic("M79 user musl blkdir probe failed");
    }

    // Capacity: 96 KiB file on CXCFS (M88 128 KiB ceiling).
    static uint8_t big_pattern[96 * 1024];
    for (uint32_t i = 0; i < sizeof(big_pattern); ++i) {
        big_pattern[i] = static_cast<uint8_t>(i ^ 0x3c);
    }
    const int64_t big_fd =
        fs::open_path("disk/big.bin", fs::o_wronly | fs::o_creat | fs::o_trunc,
                      0644);
    if (big_fd < 0) {
        log::panic("M88 disk/big.bin create failed");
    }
    if (fs::write_fd(static_cast<uint32_t>(big_fd), big_pattern,
                     sizeof(big_pattern)) !=
        static_cast<int64_t>(sizeof(big_pattern))) {
        log::panic("M88 disk/big.bin write failed");
    }
    if (fs::close_fd(static_cast<uint32_t>(big_fd)) != 0) {
        log::panic("M88 disk/big.bin close failed");
    }
    cxcfs::MountResult after_big{};
    if (!cxcfs::remount(after_big)) {
        log::panic("M88 remount after big.bin failed");
    }
    const int64_t big_in = fs::open_path("disk/big.bin", fs::o_rdonly, 0);
    if (big_in < 0) {
        log::panic("M88 disk/big.bin reopen failed");
    }
    static uint8_t big_read[96 * 1024];
    memset(big_read, 0, sizeof(big_read));
    if (fs::read_fd(static_cast<uint32_t>(big_in), big_read,
                    sizeof(big_read)) !=
        static_cast<int64_t>(sizeof(big_pattern))) {
        log::panic("M88 disk/big.bin read size failed");
    }
    (void)fs::close_fd(static_cast<uint32_t>(big_in));
    for (uint32_t i = 0; i < sizeof(big_pattern); ++i) {
        if (big_read[i] != big_pattern[i]) {
            log::panic("M88 disk/big.bin content mismatch");
        }
    }

    log::write("[ok] user musl blkdir: pass: ");
    log::decimal(musl_blkdir.wrote_pass);
    log::write(" big: 1");
    log::write(" files_cap: ");
    log::decimal(cxcfs::max_files);
    log::write(" file_bytes: ");
    log::decimal(cxcfs::max_file_bytes);
    log::write('\n');
    log::line("USER_MUSL_DIR_PASS");

    user::DiskExecProbeResult disk_exec{};
    const auto* stub_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + disk_exec_stub_elf_physical);
    const auto* payload_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + disk_exec_payload_elf_physical);
    if (disk_exec_stub_elf_physical == 0 || disk_exec_stub_elf_size == 0 ||
        disk_exec_payload_elf_physical == 0 ||
        disk_exec_payload_elf_size == 0 ||
        !user::run_disk_exec_probe(
            stub_image, static_cast<size_t>(disk_exec_stub_elf_size),
            payload_image, static_cast<size_t>(disk_exec_payload_elf_size),
            disk_exec)) {
        log::write("[debug] disk-exec stage: ");
        log::decimal(disk_exec.stage);
        log::write(", failure: ");
        log::decimal(disk_exec.failure);
        log::write(", wrote_elf: ");
        log::decimal(disk_exec.wrote_elf);
        log::write(", remount: ");
        log::decimal(disk_exec.remount_ok);
        log::write(", pass: ");
        log::decimal(disk_exec.wrote_pass);
        log::write(", exit: ");
        log::decimal(disk_exec.exit_code);
        log::write(", argv: ");
        log::decimal(disk_exec.argv_pass);
        log::write(", env: ");
        log::decimal(disk_exec.env_pass);
        log::write(", elf_bytes: ");
        log::decimal(disk_exec.elf_bytes);
        log::write('\n');
        log::panic("M82 disk execve envp probe failed");
    }
    log::write("[ok] disk-exec: pass: ");
    log::decimal(disk_exec.wrote_pass);
    log::write(" wrote_elf: ");
    log::decimal(disk_exec.wrote_elf);
    log::write(" remount: ");
    log::decimal(disk_exec.remount_ok);
    log::write('\n');
    log::line("DISK_EXEC_PASS");
    log::write("[ok] disk-argv: pass: ");
    log::decimal(disk_exec.argv_pass);
    log::write(" argc: 3 elf_bytes: ");
    log::decimal(disk_exec.elf_bytes);
    log::write('\n');
    log::line("DISK_ARGV_PASS");
    log::write("[ok] disk-env: pass: ");
    log::decimal(disk_exec.env_pass);
    log::write(" envc: 2\n");
    log::line("DISK_ENV_PASS");

    // M82: durable rename (toolchain atomic replace shape).
    constexpr char rename_payload[] = "RENAME_OK";
    const int64_t ren_fd = fs::open_path(
        "disk/ren.tmp", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
    if (ren_fd < 0) {
        log::panic("M82 disk/ren.tmp create failed");
    }
    if (fs::write_fd(static_cast<uint32_t>(ren_fd), rename_payload,
                     sizeof(rename_payload) - 1) !=
        static_cast<int64_t>(sizeof(rename_payload) - 1)) {
        log::panic("M82 disk/ren.tmp write failed");
    }
    if (fs::close_fd(static_cast<uint32_t>(ren_fd)) != 0) {
        log::panic("M82 disk/ren.tmp close failed");
    }
    if (fs::rename_path("disk/ren.tmp", "disk/ren.ok") != 0) {
        log::panic("M82 rename disk/ren.tmp -> disk/ren.ok failed");
    }
    // Edge: missing source rejected.
    if (fs::rename_path("disk/ren.missing", "disk/ren.x") != -2) {
        log::panic("M82 rename missing-source edge failed");
    }
    // Edge: cross-backend rejected.
    if (fs::rename_path("disk/ren.ok", "ram-ren.ok") != -1) {
        log::panic("M82 rename cross-backend edge failed");
    }
    cxcfs::MountResult after_ren{};
    if (!cxcfs::remount(after_ren)) {
        log::panic("M82 remount after rename failed");
    }
    if (fs::open_path("disk/ren.tmp", fs::o_rdonly, 0) >= 0) {
        log::panic("M82 old name still visible after rename");
    }
    const int64_t ok_fd = fs::open_path("disk/ren.ok", fs::o_rdonly, 0);
    if (ok_fd < 0) {
        log::panic("M82 disk/ren.ok missing after remount");
    }
    char ren_buf[16]{};
    if (fs::read_fd(static_cast<uint32_t>(ok_fd), ren_buf, sizeof(ren_buf)) !=
        static_cast<int64_t>(sizeof(rename_payload) - 1)) {
        log::panic("M82 disk/ren.ok read size failed");
    }
    (void)fs::close_fd(static_cast<uint32_t>(ok_fd));
    for (size_t i = 0; i < sizeof(rename_payload) - 1; ++i) {
        if (ren_buf[i] != rename_payload[i]) {
            log::panic("M82 disk/ren.ok content mismatch");
        }
    }
    log::write("[ok] disk-rename: pass: 1 edge_miss: 1 edge_xdev: 1\n");
    log::line("DISK_RENAME_PASS");

    user::ForkWaitProbeResult fork_wait{};
    const auto* fork_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + fork_wait_elf_physical);
    if (fork_wait_elf_physical == 0 || fork_wait_elf_size == 0 ||
        !user::run_fork_wait_probe(
            fork_image, static_cast<size_t>(fork_wait_elf_size), fork_wait)) {
        log::write("[debug] fork-wait stage: ");
        log::decimal(fork_wait.stage);
        log::write(", failure: ");
        log::decimal(fork_wait.failure);
        log::write(", pass: ");
        log::decimal(fork_wait.wrote_pass);
        log::write(", exit: ");
        log::decimal(fork_wait.exit_code);
        log::write('\n');
        log::panic("M83 fork/wait probe failed");
    }
    log::write("[ok] fork-wait: pass: ");
    log::decimal(fork_wait.wrote_pass);
    log::write(" child: 1 status: 42\n");
    log::line("FORK_WAIT_PASS");

    // M85 kernel-side pipe smoke (before ELF probe).
    {
        fs::reset();
        int32_t pfds[2]{-1, -1};
        if (fs::pipe_fds(pfds) != 0 || pfds[0] < 0 || pfds[1] < 0) {
            log::panic("M85 kernel pipe_fds failed");
        }
        const char payload[] = "PIPE_DATA\n";
        if (fs::write_fd(static_cast<uint32_t>(pfds[1]), payload,
                         sizeof(payload) - 1) !=
            static_cast<int64_t>(sizeof(payload) - 1)) {
            log::panic("M85 kernel pipe write failed");
        }
        if (fs::close_fd(static_cast<uint32_t>(pfds[1])) != 0) {
            log::panic("M85 kernel pipe close-w failed");
        }
        char buf[16]{};
        if (fs::read_fd(static_cast<uint32_t>(pfds[0]), buf,
                        sizeof(payload) - 1) !=
            static_cast<int64_t>(sizeof(payload) - 1)) {
            log::panic("M85 kernel pipe read failed");
        }
        for (size_t i = 0; i < sizeof(payload) - 1; ++i) {
            if (buf[i] != payload[i]) {
                log::panic("M85 kernel pipe content mismatch");
            }
        }
        if (fs::close_fd(static_cast<uint32_t>(pfds[0])) != 0) {
            log::panic("M85 kernel pipe close-r failed");
        }
        // EPIPE: write with no readers.
        if (fs::pipe_fds(pfds) != 0) {
            log::panic("M85 kernel pipe_fds (epipe) failed");
        }
        if (fs::close_fd(static_cast<uint32_t>(pfds[0])) != 0) {
            log::panic("M85 kernel epipe close-r failed");
        }
        if (fs::write_fd(static_cast<uint32_t>(pfds[1]), "X", 1) != -32) {
            log::panic("M85 kernel EPIPE edge failed");
        }
        (void)fs::close_fd(static_cast<uint32_t>(pfds[1]));
        // dup: write via duplicated end.
        if (fs::pipe_fds(pfds) != 0) {
            log::panic("M85 kernel pipe_fds (dup) failed");
        }
        const int64_t duped = fs::dup_fd(static_cast<uint32_t>(pfds[1]));
        if (duped < 0) {
            log::panic("M85 kernel dup failed");
        }
        if (fs::close_fd(static_cast<uint32_t>(pfds[1])) != 0) {
            log::panic("M85 kernel dup close-old failed");
        }
        if (fs::write_fd(static_cast<uint32_t>(duped), "D", 1) != 1) {
            log::panic("M85 kernel dup write failed");
        }
        (void)fs::close_fd(static_cast<uint32_t>(duped));
        char dbuf[4]{};
        if (fs::read_fd(static_cast<uint32_t>(pfds[0]), dbuf, 1) != 1 ||
            dbuf[0] != 'D') {
            log::panic("M85 kernel dup read failed");
        }
        (void)fs::close_fd(static_cast<uint32_t>(pfds[0]));
        fs::reset();
        log::write("[ok] pipe-k: pass: 1 epipe: 1 dup: 1\n");
    }

    user::PipeProbeResult pipe_probe{};
    const auto* pipe_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + pipe_elf_physical);
    if (pipe_elf_physical == 0 || pipe_elf_size == 0 ||
        !user::run_pipe_probe(pipe_image, static_cast<size_t>(pipe_elf_size),
                              pipe_probe)) {
        log::write("[debug] pipe stage: ");
        log::decimal(pipe_probe.stage);
        log::write(", failure: ");
        log::decimal(pipe_probe.failure);
        log::write(", pass: ");
        log::decimal(pipe_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(pipe_probe.exit_code);
        log::write('\n');
        log::panic("M85 pipe/dup probe failed");
    }
    log::write("[ok] pipe: pass: ");
    log::decimal(pipe_probe.wrote_pass);
    log::write(" fork_ipc: 1 epipe: 1 dup: 1\n");
    log::line("PIPE_PASS");

    // M86 kernel-side chdir/getcwd smoke.
    {
        fs::reset();
        if (fs::mkdir_path("cwdtest", 0755) != 0) {
            log::panic("M86 kernel mkdir failed");
        }
        if (fs::chdir_path("cwdtest") != 0) {
            log::panic("M86 kernel chdir failed");
        }
        char cwd_buf[32]{};
        if (fs::getcwd_path(cwd_buf, sizeof(cwd_buf)) != 9) {
            log::panic("M86 kernel getcwd len failed");
        }
        const char expect[] = "/cwdtest";
        for (size_t i = 0; i < sizeof(expect); ++i) {
            if (cwd_buf[i] != expect[i]) {
                log::panic("M86 kernel getcwd content failed");
            }
        }
        const int64_t nfd =
            fs::open_path("note.txt", fs::o_rdwr | fs::o_creat | fs::o_trunc, 0644);
        if (nfd < 0) {
            log::panic("M86 kernel relative open failed");
        }
        if (fs::write_fd(static_cast<uint32_t>(nfd), "HI", 2) != 2) {
            log::panic("M86 kernel relative write failed");
        }
        (void)fs::close_fd(static_cast<uint32_t>(nfd));
        if (fs::chdir_path("..") != 0) {
            log::panic("M86 kernel chdir .. failed");
        }
        if (fs::chdir_path("no_such_dir") != -2) {
            log::panic("M86 kernel ENOENT edge failed");
        }
        if (fs::getcwd_path(cwd_buf, 1) != -34) {
            log::panic("M86 kernel ERANGE edge failed");
        }
        fs::reset();
        log::write("[ok] chdir-k: pass: 1 enoent: 1 erange: 1\n");
    }

    user::ChdirProbeResult chdir_probe{};
    const auto* chdir_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + chdir_elf_physical);
    if (chdir_elf_physical == 0 || chdir_elf_size == 0 ||
        !user::run_chdir_probe(chdir_image, static_cast<size_t>(chdir_elf_size),
                               chdir_probe)) {
        log::write("[debug] chdir stage: ");
        log::decimal(chdir_probe.stage);
        log::write(", failure: ");
        log::decimal(chdir_probe.failure);
        log::write(", pass: ");
        log::decimal(chdir_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(chdir_probe.exit_code);
        log::write('\n');
        log::panic("M86 chdir/getcwd probe failed");
    }
    log::write("[ok] chdir: pass: ");
    log::decimal(chdir_probe.wrote_pass);
    log::write(" rel: 1 enoent: 1 erange: 1\n");
    log::line("CHDIR_PASS");

    // M87 kernel nested mkdir on disk/ + orphan parent edge.
    {
        if (fs::mkdir_path("/disk/knest", 0755) != 0) {
            log::panic("M87 kernel mkdir knest failed");
        }
        if (fs::mkdir_path("/disk/knest/sub", 0755) != 0) {
            log::panic("M87 kernel mkdir knest/sub failed");
        }
        if (fs::mkdir_path("/disk/nope/leaf", 0755) == 0) {
            log::panic("M87 kernel orphan mkdir should fail");
        }
        const int64_t nfd = fs::open_path(
            "/disk/knest/sub/k.txt", fs::o_rdwr | fs::o_creat | fs::o_trunc,
            0644);
        if (nfd < 0) {
            log::panic("M87 kernel nested open failed");
        }
        if (fs::write_fd(static_cast<uint32_t>(nfd), "NEST", 4) != 4) {
            log::panic("M87 kernel nested write failed");
        }
        (void)fs::close_fd(static_cast<uint32_t>(nfd));
        cxcfs::MountResult remount{};
        if (!cxcfs::remount(remount)) {
            log::panic("M87 kernel remount after nest failed");
        }
        const int64_t rfd =
            fs::open_path("/disk/knest/sub/k.txt", fs::o_rdonly, 0);
        if (rfd < 0) {
            log::panic("M87 kernel nest reopen failed");
        }
        char buf[8]{};
        if (fs::read_fd(static_cast<uint32_t>(rfd), buf, 4) != 4 ||
            buf[0] != 'N' || buf[1] != 'E' || buf[2] != 'S' || buf[3] != 'T') {
            log::panic("M87 kernel nest persist mismatch");
        }
        (void)fs::close_fd(static_cast<uint32_t>(rfd));
        log::write("[ok] nest-k: pass: 1 orphan: 1 persist: 1\n");
    }

    user::NestProbeResult nest_probe{};
    const auto* nest_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + nest_elf_physical);
    if (nest_elf_physical == 0 || nest_elf_size == 0 ||
        !user::run_nest_probe(nest_image, static_cast<size_t>(nest_elf_size),
                              nest_probe)) {
        log::write("[debug] nest stage: ");
        log::decimal(nest_probe.stage);
        log::write(", failure: ");
        log::decimal(nest_probe.failure);
        log::write(", pass: ");
        log::decimal(nest_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(nest_probe.exit_code);
        log::write('\n');
        log::panic("M87 nest/dirs probe failed");
    }
    log::write("[ok] nest: pass: ");
    log::decimal(nest_probe.wrote_pass);
    log::write(" depth: 2 orphan: 1\n");
    log::line("NEST_PASS");

    // M88: userspace 96 KiB CXCFS write/read (ceiling 128 KiB).
    user::CapProbeResult cap_probe{};
    const auto* cap_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + cap_elf_physical);
    if (cap_elf_physical == 0 || cap_elf_size == 0 ||
        !user::run_cap_probe(cap_image, static_cast<size_t>(cap_elf_size),
                             cap_probe)) {
        log::write("[debug] cap stage: ");
        log::decimal(cap_probe.stage);
        log::write(", failure: ");
        log::decimal(cap_probe.failure);
        log::write(", pass: ");
        log::decimal(cap_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(cap_probe.exit_code);
        log::write('\n');
        log::panic("M88 capacity probe failed");
    }
    log::write("[ok] cap: pass: ");
    log::decimal(cap_probe.wrote_pass);
    log::write(" bytes: 98304 ceiling: ");
    log::decimal(cxcfs::max_file_bytes);
    log::write('\n');
    log::line("CAP_PASS");

    // M89: >64 KiB ET_EXEC on disk/ via execve (loader ceiling 128 KiB).
    user::Load128ProbeResult load128{};
    const auto* load128_stub = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + load128_stub_elf_physical);
    const auto* load128_payload = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + load128_payload_elf_physical);
    if (load128_stub_elf_physical == 0 || load128_stub_elf_size == 0 ||
        load128_payload_elf_physical == 0 || load128_payload_elf_size == 0 ||
        !user::run_load128_probe(
            load128_stub, static_cast<size_t>(load128_stub_elf_size),
            load128_payload, static_cast<size_t>(load128_payload_elf_size),
            load128)) {
        log::write("[debug] load128 stage: ");
        log::decimal(load128.stage);
        log::write(", failure: ");
        log::decimal(load128.failure);
        log::write(", wrote_elf: ");
        log::decimal(load128.wrote_elf);
        log::write(", remount: ");
        log::decimal(load128.remount_ok);
        log::write(", pass: ");
        log::decimal(load128.wrote_pass);
        log::write(", exit: ");
        log::decimal(load128.exit_code);
        log::write(", elf_bytes: ");
        log::decimal(load128.elf_bytes);
        log::write('\n');
        log::panic("M89 load128 execve probe failed");
    }
    log::write("[ok] load128: pass: ");
    log::decimal(load128.wrote_pass);
    log::write(" elf_bytes: ");
    log::decimal(load128.elf_bytes);
    log::write(" remount: ");
    log::decimal(load128.remount_ok);
    log::write('\n');
    log::line("LOAD128_PASS");

    // M90: brk 768 KiB + mmap 512 KiB (ceilings 1 MiB each; stack @ 0x800000).
    user::HeapProbeResult heap_probe{};
    const auto* heap_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + heap_elf_physical);
    if (heap_elf_physical == 0 || heap_elf_size == 0 ||
        !user::run_heap_probe(heap_image, static_cast<size_t>(heap_elf_size),
                              heap_probe)) {
        log::write("[debug] heap stage: ");
        log::decimal(heap_probe.stage);
        log::write(", failure: ");
        log::decimal(heap_probe.failure);
        log::write(", pass: ");
        log::decimal(heap_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(heap_probe.exit_code);
        log::write('\n');
        log::panic("M90 heap/mmap expand probe failed");
    }
    log::write("[ok] heap: pass: ");
    log::decimal(heap_probe.wrote_pass);
    log::write(" brk_kib: 768 mmap_kib: 512 ceiling_kib: 1024\n");
    log::line("HEAP_PASS");

    // M91: access + clock_gettime + gettimeofday (toolchain early path).
    user::TimeProbeResult time_probe{};
    const auto* time_image = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + time_elf_physical);
    if (time_elf_physical == 0 || time_elf_size == 0 ||
        !user::run_time_probe(time_image, static_cast<size_t>(time_elf_size),
                              time_probe)) {
        log::write("[debug] time stage: ");
        log::decimal(time_probe.stage);
        log::write(", failure: ");
        log::decimal(time_probe.failure);
        log::write(", pass: ");
        log::decimal(time_probe.wrote_pass);
        log::write(", exit: ");
        log::decimal(time_probe.exit_code);
        log::write('\n');
        log::panic("M91 access/time probe failed");
    }
    log::write("[ok] time: pass: ");
    log::decimal(time_probe.wrote_pass);
    log::write(" access: 1 clock: 1 gettimeofday: 1\n");
    log::line("TIME_PASS");

    // M124/A2: strcpy→__stpcpy + strncmp + .rodata → NMSG + WMSG + SCPY.
    user::OnboxCcProbeResult onbox{};
    const auto* onbox_stub = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + onbox_stub_elf_physical);
    const auto* onbox_cc = reinterpret_cast<const uint8_t*>(
        vmm::direct_map_base + onbox_cc_elf_physical);
    if (onbox_stub_elf_physical == 0 || onbox_stub_elf_size == 0 ||
        onbox_cc_elf_physical == 0 || onbox_cc_elf_size == 0 ||
        !user::run_onbox_cc_probe(
            onbox_stub, static_cast<size_t>(onbox_stub_elf_size), onbox_cc,
            static_cast<size_t>(onbox_cc_elf_size), onbox)) {
        log::write("[debug] onbox-cc stage: ");
        log::decimal(onbox.stage);
        log::write(", failure: ");
        log::decimal(onbox.failure);
        log::write(", src: ");
        log::decimal(onbox.wrote_src);
        log::write(", cc: ");
        log::decimal(onbox.wrote_cc);
        log::write(", remount: ");
        log::decimal(onbox.remount_ok);
        log::write(", pass: ");
        log::decimal(onbox.wrote_pass);
        log::write(", edge: ");
        log::decimal(onbox.edge_ok);
        log::write(", exit: ");
        log::decimal(onbox.exit_code);
        log::write('\n');
        log::panic("M124 on-box mini-cc strcpy/stpcpy + strncmp pipeline failed");
    }
    log::write("[ok] onbox-cc: pass: ");
    log::decimal(onbox.wrote_pass);
    log::write(" src: 1 cc: 1 run: 1 exit: ");
    log::decimal(onbox.exit_code);
    log::write(" write: 1 scpy: 1 edge: ");
    log::decimal(onbox.edge_ok);
    log::write('\n');
    log::line("ONBOX_SCPY_PASS");

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
    if (!relocate_esp_elf_to_pmm(libc_fs_elf_physical, libc_fs_elf_size,
                                 "ESP LIBFS.ELF")) {
        log::panic("cannot relocate ESP LIBFS.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_sbrk_elf_physical, libc_sbrk_elf_size,
                                 "ESP LIBBRK.ELF")) {
        log::panic("cannot relocate ESP LIBBRK.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_malloc_elf_physical, libc_malloc_elf_size,
                                 "ESP LIBMAL.ELF")) {
        log::panic("cannot relocate ESP LIBMAL.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_errno_elf_physical, libc_errno_elf_size,
                                 "ESP LIBERR.ELF")) {
        log::panic("cannot relocate ESP LIBERR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_string_elf_physical, libc_string_elf_size,
                                 "ESP LIBSTR.ELF")) {
        log::panic("cannot relocate ESP LIBSTR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_writev_elf_physical, libc_writev_elf_size,
                                 "ESP LIBWRV.ELF")) {
        log::panic("cannot relocate ESP LIBWRV.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_crt_elf_physical, libc_crt_elf_size,
                                 "ESP LIBCRT.ELF")) {
        log::panic("cannot relocate ESP LIBCRT.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_uname_elf_physical, libc_uname_elf_size,
                                 "ESP LIBUNA.ELF")) {
        log::panic("cannot relocate ESP LIBUNA.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_tid_elf_physical, libc_tid_elf_size,
                                 "ESP LIBTID.ELF")) {
        log::panic("cannot relocate ESP LIBTID.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(libc_pid_elf_physical, libc_pid_elf_size,
                                 "ESP LIBPID.ELF")) {
        log::panic("cannot relocate ESP LIBPID.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_write_elf_physical, musl_write_elf_size,
                                 "ESP LIBMSL.ELF")) {
        log::panic("cannot relocate ESP LIBMSL.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_auxv_elf_physical, musl_auxv_elf_size,
                                 "ESP LIBMAV.ELF")) {
        log::panic("cannot relocate ESP LIBMAV.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_crt_elf_physical, musl_crt_elf_size,
                                 "ESP LIBMCR.ELF")) {
        log::panic("cannot relocate ESP LIBMCR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_malloc_elf_physical, musl_malloc_elf_size,
                                 "ESP LIBMMA.ELF")) {
        log::panic("cannot relocate ESP LIBMMA.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_printf_elf_physical, musl_printf_elf_size,
                                 "ESP LIBMPR.ELF")) {
        log::panic("cannot relocate ESP LIBMPR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_fopen_elf_physical, musl_fopen_elf_size,
                                 "ESP LIBFOP.ELF")) {
        log::panic("cannot relocate ESP LIBFOP.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_opendir_elf_physical,
                                 musl_opendir_elf_size, "ESP LIBODR.ELF")) {
        log::panic("cannot relocate ESP LIBODR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_unlink_elf_physical, musl_unlink_elf_size,
                                 "ESP LIBUNL.ELF")) {
        log::panic("cannot relocate ESP LIBUNL.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_blkfs_elf_physical, musl_blkfs_elf_size,
                                 "ESP LIBBLK.ELF")) {
        log::panic("cannot relocate ESP LIBBLK.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(musl_blkdir_elf_physical, musl_blkdir_elf_size,
                                 "ESP LIBDIR.ELF")) {
        log::panic("cannot relocate ESP LIBDIR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(disk_exec_stub_elf_physical,
                                 disk_exec_stub_elf_size, "ESP EXSTB.ELF")) {
        log::panic("cannot relocate ESP EXSTB.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(disk_exec_payload_elf_physical,
                                 disk_exec_payload_elf_size, "ESP DEXEC.ELF")) {
        log::panic("cannot relocate ESP DEXEC.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(fork_wait_elf_physical, fork_wait_elf_size,
                                 "ESP FORK.ELF")) {
        log::panic("cannot relocate ESP FORK.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(pipe_elf_physical, pipe_elf_size,
                                 "ESP PIPE.ELF")) {
        log::panic("cannot relocate ESP PIPE.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(chdir_elf_physical, chdir_elf_size,
                                 "ESP CHDIR.ELF")) {
        log::panic("cannot relocate ESP CHDIR.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(nest_elf_physical, nest_elf_size,
                                 "ESP NEST.ELF")) {
        log::panic("cannot relocate ESP NEST.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(cap_elf_physical, cap_elf_size,
                                 "ESP CAP.ELF")) {
        log::panic("cannot relocate ESP CAP.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(load128_stub_elf_physical,
                                 load128_stub_elf_size, "ESP L128S.ELF")) {
        log::panic("cannot relocate ESP L128S.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(load128_payload_elf_physical,
                                 load128_payload_elf_size, "ESP L128.ELF")) {
        log::panic("cannot relocate ESP L128.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(heap_elf_physical, heap_elf_size,
                                 "ESP HEAP.ELF")) {
        log::panic("cannot relocate ESP HEAP.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(time_elf_physical, time_elf_size,
                                 "ESP TIME.ELF")) {
        log::panic("cannot relocate ESP TIME.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(onbox_stub_elf_physical, onbox_stub_elf_size,
                                 "ESP ONBXST.ELF")) {
        log::panic("cannot relocate ESP ONBXST.ELF into PMM");
    }
    if (!relocate_esp_elf_to_pmm(onbox_cc_elf_physical, onbox_cc_elf_size,
                                 "ESP ONBXCC.ELF")) {
        log::panic("cannot relocate ESP ONBXCC.ELF into PMM");
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
    cxc::log::line("CXC/OS 2.15.0 - on-box mini-cc strcpy/stpcpy midfield (A2/M124)");

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

    static CHAR16 libc_fs_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'F', u'S',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_fs_elf_path, 64 * 1024,
        cxc::libc_fs_elf_physical, cxc::libc_fs_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBFS.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBFS.ELF bytes: ");
    cxc::log::decimal(cxc::libc_fs_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_sbrk_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'B', u'R',  u'K', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_sbrk_elf_path, 64 * 1024,
        cxc::libc_sbrk_elf_physical, cxc::libc_sbrk_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBBRK.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBBRK.ELF bytes: ");
    cxc::log::decimal(cxc::libc_sbrk_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_malloc_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'A',  u'L', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_malloc_elf_path, 64 * 1024,
        cxc::libc_malloc_elf_physical, cxc::libc_malloc_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMAL.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMAL.ELF bytes: ");
    cxc::log::decimal(cxc::libc_malloc_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_errno_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'E', u'R',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_errno_elf_path, 64 * 1024,
        cxc::libc_errno_elf_physical, cxc::libc_errno_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBERR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBERR.ELF bytes: ");
    cxc::log::decimal(cxc::libc_errno_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_string_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'S', u'T',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_string_elf_path, 64 * 1024,
        cxc::libc_string_elf_physical, cxc::libc_string_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBSTR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBSTR.ELF bytes: ");
    cxc::log::decimal(cxc::libc_string_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_writev_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'W', u'R',  u'V', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_writev_elf_path, 64 * 1024,
        cxc::libc_writev_elf_physical, cxc::libc_writev_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBWRV.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBWRV.ELF bytes: ");
    cxc::log::decimal(cxc::libc_writev_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_crt_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'C', u'R',  u'T', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_crt_elf_path, 64 * 1024,
        cxc::libc_crt_elf_physical, cxc::libc_crt_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBCRT.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBCRT.ELF bytes: ");
    cxc::log::decimal(cxc::libc_crt_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_uname_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'U', u'N',  u'A', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_uname_elf_path, 64 * 1024,
        cxc::libc_uname_elf_physical, cxc::libc_uname_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBUNA.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBUNA.ELF bytes: ");
    cxc::log::decimal(cxc::libc_uname_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_tid_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'T', u'I',  u'D', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_tid_elf_path, 64 * 1024,
        cxc::libc_tid_elf_physical, cxc::libc_tid_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBTID.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBTID.ELF bytes: ");
    cxc::log::decimal(cxc::libc_tid_elf_size);
    cxc::log::write('\n');

    static CHAR16 libc_pid_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'P', u'I',  u'D', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, libc_pid_elf_path, 64 * 1024,
        cxc::libc_pid_elf_physical, cxc::libc_pid_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBPID.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBPID.ELF bytes: ");
    cxc::log::decimal(cxc::libc_pid_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_write_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'S',  u'L', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_write_elf_path, 64 * 1024,
        cxc::musl_write_elf_physical, cxc::musl_write_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMSL.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMSL.ELF bytes: ");
    cxc::log::decimal(cxc::musl_write_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_auxv_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'A',  u'V', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_auxv_elf_path, 64 * 1024,
        cxc::musl_auxv_elf_physical, cxc::musl_auxv_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMAV.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMAV.ELF bytes: ");
    cxc::log::decimal(cxc::musl_auxv_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_crt_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'C',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_crt_elf_path, 64 * 1024,
        cxc::musl_crt_elf_physical, cxc::musl_crt_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMCR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMCR.ELF bytes: ");
    cxc::log::decimal(cxc::musl_crt_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_malloc_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'M',  u'A', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_malloc_elf_path, 64 * 1024,
        cxc::musl_malloc_elf_physical, cxc::musl_malloc_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMMA.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMMA.ELF bytes: ");
    cxc::log::decimal(cxc::musl_malloc_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_printf_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'M', u'P',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_printf_elf_path, 64 * 1024,
        cxc::musl_printf_elf_physical, cxc::musl_printf_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBMPR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBMPR.ELF bytes: ");
    cxc::log::decimal(cxc::musl_printf_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_fopen_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'F', u'O',  u'P', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_fopen_elf_path, 64 * 1024,
        cxc::musl_fopen_elf_physical, cxc::musl_fopen_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBFOP.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBFOP.ELF bytes: ");
    cxc::log::decimal(cxc::musl_fopen_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_opendir_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'O', u'D',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_opendir_elf_path, 64 * 1024,
        cxc::musl_opendir_elf_physical, cxc::musl_opendir_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBODR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBODR.ELF bytes: ");
    cxc::log::decimal(cxc::musl_opendir_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_unlink_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'U', u'N',  u'L', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_unlink_elf_path, 64 * 1024,
        cxc::musl_unlink_elf_physical, cxc::musl_unlink_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBUNL.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBUNL.ELF bytes: ");
    cxc::log::decimal(cxc::musl_unlink_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_blkfs_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'B', u'L',  u'K', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_blkfs_elf_path, 64 * 1024,
        cxc::musl_blkfs_elf_physical, cxc::musl_blkfs_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBBLK.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBBLK.ELF bytes: ");
    cxc::log::decimal(cxc::musl_blkfs_elf_size);
    cxc::log::write('\n');

    static CHAR16 musl_blkdir_elf_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'I', u'B', u'D', u'I',  u'R', u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, musl_blkdir_elf_path, 64 * 1024,
        cxc::musl_blkdir_elf_physical, cxc::musl_blkdir_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP LIBDIR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP LIBDIR.ELF bytes: ");
    cxc::log::decimal(cxc::musl_blkdir_elf_size);
    cxc::log::write('\n');

    static CHAR16 disk_exec_stub_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'E',  u'X', u'S', u'T', u'B',  u'.', u'E', u'L',  u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, disk_exec_stub_path, 64 * 1024,
        cxc::disk_exec_stub_elf_physical, cxc::disk_exec_stub_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP EXSTB.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP EXSTB.ELF bytes: ");
    cxc::log::decimal(cxc::disk_exec_stub_elf_size);
    cxc::log::write('\n');

    static CHAR16 disk_exec_payload_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'D',  u'E', u'X', u'E', u'C',  u'.', u'E', u'L',  u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, disk_exec_payload_path, 64 * 1024,
        cxc::disk_exec_payload_elf_physical, cxc::disk_exec_payload_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP DEXEC.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP DEXEC.ELF bytes: ");
    cxc::log::decimal(cxc::disk_exec_payload_elf_size);
    cxc::log::write('\n');

    static CHAR16 fork_wait_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'F',  u'O', u'R', u'K', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, fork_wait_path, 64 * 1024,
        cxc::fork_wait_elf_physical, cxc::fork_wait_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP FORK.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP FORK.ELF bytes: ");
    cxc::log::decimal(cxc::fork_wait_elf_size);
    cxc::log::write('\n');

    static CHAR16 pipe_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'P',  u'I', u'P', u'E', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, pipe_path,
                                      64 * 1024, cxc::pipe_elf_physical,
                                      cxc::pipe_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP PIPE.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP PIPE.ELF bytes: ");
    cxc::log::decimal(cxc::pipe_elf_size);
    cxc::log::write('\n');

    static CHAR16 chdir_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'C',  u'H', u'D', u'I', u'R',  u'.', u'E', u'L',  u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, chdir_path,
                                      64 * 1024, cxc::chdir_elf_physical,
                                      cxc::chdir_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP CHDIR.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP CHDIR.ELF bytes: ");
    cxc::log::decimal(cxc::chdir_elf_size);
    cxc::log::write('\n');

    static CHAR16 nest_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'N',  u'E', u'S', u'T', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, nest_path,
                                      64 * 1024, cxc::nest_elf_physical,
                                      cxc::nest_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP NEST.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP NEST.ELF bytes: ");
    cxc::log::decimal(cxc::nest_elf_size);
    cxc::log::write('\n');

    static CHAR16 cap_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'C',  u'A', u'P', u'.',  u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, cap_path,
                                      64 * 1024, cxc::cap_elf_physical,
                                      cxc::cap_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP CAP.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP CAP.ELF bytes: ");
    cxc::log::decimal(cxc::cap_elf_size);
    cxc::log::write('\n');

    static CHAR16 load128_stub_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'1', u'2', u'8', u'S',  u'.', u'E',  u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, load128_stub_path, 64 * 1024,
        cxc::load128_stub_elf_physical, cxc::load128_stub_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP L128S.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP L128S.ELF bytes: ");
    cxc::log::decimal(cxc::load128_stub_elf_size);
    cxc::log::write('\n');

    static CHAR16 load128_payload_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'L',  u'1', u'2', u'8', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, load128_payload_path, 64 * 1024,
        cxc::load128_payload_elf_physical, cxc::load128_payload_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP L128.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP L128.ELF bytes: ");
    cxc::log::decimal(cxc::load128_payload_elf_size);
    cxc::log::write('\n');

    static CHAR16 heap_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'H',  u'E', u'A', u'P', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, heap_path,
                                      64 * 1024, cxc::heap_elf_physical,
                                      cxc::heap_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP HEAP.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP HEAP.ELF bytes: ");
    cxc::log::decimal(cxc::heap_elf_size);
    cxc::log::write('\n');

    static CHAR16 time_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'T',  u'I', u'M', u'E', u'.',  u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(image_handle, system_table, time_path,
                                      64 * 1024, cxc::time_elf_physical,
                                      cxc::time_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP TIME.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP TIME.ELF bytes: ");
    cxc::log::decimal(cxc::time_elf_size);
    cxc::log::write('\n');

    static CHAR16 onbox_stub_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'O',  u'N', u'B', u'X', u'S',  u'T', u'.', u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, onbox_stub_path, 64 * 1024,
        cxc::onbox_stub_elf_physical, cxc::onbox_stub_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP ONBXST.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP ONBXST.ELF bytes: ");
    cxc::log::decimal(cxc::onbox_stub_elf_size);
    cxc::log::write('\n');

    static CHAR16 onbox_cc_path[] = {
        u'\\', u'E', u'F', u'I', u'\\', u'B', u'O', u'O', u'T', u'\\',
        u'O',  u'N', u'B', u'X', u'C',  u'C', u'.', u'E', u'L', u'F', 0};
    status = cxc::uefi::load_esp_file(
        image_handle, system_table, onbox_cc_path, 128 * 1024,
        cxc::onbox_cc_elf_physical, cxc::onbox_cc_elf_size);
    if (efi_error(status)) {
        cxc::log::write("[boot] ESP ONBXCC.ELF load failed, status: ");
        cxc::log::hex(status);
        cxc::log::write('\n');
        return status;
    }
    cxc::log::write("[ok] ESP ONBXCC.ELF bytes: ");
    cxc::log::decimal(cxc::onbox_cc_elf_size);
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
