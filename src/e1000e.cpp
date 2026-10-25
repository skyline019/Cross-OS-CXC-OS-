#include <cxc/e1000e.hpp>

#include "e1000e_detail.hpp"

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/irq.hpp>
#include <cxc/log.hpp>
#include <cxc/msix.hpp>
#include <cxc/net/checksum.hpp>
#include <cxc/net/proto.hpp>
#include <cxc/net/ring_model.hpp>
#include <cxc/pci.hpp>
#include <cxc/pmm.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void*, int, size_t);

namespace cxc::e1000e {
namespace {

constexpr uint16_t intel_vendor = 0x8086;
constexpr uint16_t device_82574l = 0x10d3;
constexpr uint64_t msix_virtual_base = 0xffffffff40010000;
constexpr uint64_t maximum_msix_mapping = 64 * 1024;
constexpr uint64_t csr_mapping_bytes = 0x8000;

constexpr uint32_t reg_ims = 0x00d0;
constexpr uint32_t reg_imc = 0x00d8;
constexpr uint32_t reg_eiac = 0x00dc;
constexpr uint32_t reg_iam = 0x00e0;
constexpr uint32_t reg_ivar = 0x00e4;
constexpr uint32_t reg_eitr = 0x00e8;

constexpr uint32_t cause_rxq1 = 0x00200000;
constexpr uint32_t tested_causes = detail::cause_rxq0 | cause_rxq1 | detail::cause_txq0;

constexpr uint16_t command_memory = 1 << 1;
constexpr uint16_t command_bus_master = 1 << 2;
constexpr uint16_t command_intx_disable = 1 << 10;
constexpr uint16_t msi_enable = 1 << 0;
constexpr uint16_t msix_enable = 1 << 15;
constexpr uint16_t msix_function_mask = 1 << 14;
constexpr uint32_t ivar_valid = 0x8;

struct HandlerContext {
    VectorResult* result;
};
HandlerContext contexts[3];

void interrupt_handler(void* opaque) {
    auto* context = static_cast<HandlerContext*>(opaque);
    VectorResult* result = context->result;

    // QEMU may issue an empty notification when its minimum EITR throttle
    // timer expires. ICS reflects ICR without read-to-clear side effects, so
    // an empty/foreign vector cannot accidentally clear another vector's cause.
    const uint32_t cause = detail::mmio_read(detail::reg_ics);
    const uint32_t payload = cause & ~detail::icr_asserted;
    __atomic_add_fetch(&result->calls, 1, __ATOMIC_RELAXED);
    if (payload == 0) {
        __atomic_add_fetch(&result->empty_calls, 1, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&result->last_cause, cause, __ATOMIC_RELAXED);
    const bool packet_phase =
        __atomic_load_n(&detail::packet_active, __ATOMIC_ACQUIRE);
    const uint32_t allowed_causes =
        packet_phase ? (detail::packet_queue_causes | detail::auxiliary_causes)
                     : result->expected_cause;
    if ((payload & ~allowed_causes) != 0) {
        __atomic_store_n(&result->error_cause, cause, __ATOMIC_RELAXED);
        __atomic_add_fetch(&result->errors, 1, __ATOMIC_RELEASE);
        return;
    }
    if ((payload & result->expected_cause) == 0) {
        // Link setup and TX queue draining can leave LSC/TXQE latched even
        // though IMS never enabled them. A later queue/EITR notification can
        // observe only those auxiliary causes. Acknowledge just those bits
        // during packet/protocol/UDP phases; every other foreign cause fails.
        if (packet_phase && (payload & ~detail::auxiliary_causes) == 0) {
            detail::mmio_write(detail::reg_icr, payload);
            return;
        }
        // EITR can notify one entry while ICS still names another entry's
        // queue. Do not steal/clear that queue's cause or count it as our own
        // matched event. Its owner remains responsible for acknowledgement.
        if (packet_phase &&
            (payload & ~(detail::packet_queue_causes | detail::auxiliary_causes)) == 0) {
            __atomic_add_fetch(&result->cross_calls, 1, __ATOMIC_RELAXED);
            if ((payload & detail::auxiliary_causes) != 0) {
                detail::mmio_write(detail::reg_icr, payload & detail::auxiliary_causes);
            }
            return;
        }
        __atomic_store_n(&result->error_cause, cause, __ATOMIC_RELAXED);
        __atomic_add_fetch(&result->errors, 1, __ATOMIC_RELEASE);
        return;
    }

    __atomic_add_fetch(&result->matched_calls, 1, __ATOMIC_RELEASE);
    if (packet_phase) {
        const bool protocol_phase =
            __atomic_load_n(&detail::protocol_active, __ATOMIC_ACQUIRE);
        const bool udp_phase = __atomic_load_n(&detail::udp_active, __ATOMIC_ACQUIRE);
        const bool udp_stress_phase =
            __atomic_load_n(&detail::udp_stress_active, __ATOMIC_ACQUIRE);
        const bool socket_tx_phase =
            __atomic_load_n(&detail::socket_tx_active, __ATOMIC_ACQUIRE);
        const bool socket_tx_bp_phase =
            __atomic_load_n(&detail::socket_tx_bp_active, __ATOMIC_ACQUIRE);
        const bool socket_tx_wait_phase =
            __atomic_load_n(&detail::socket_tx_wait_active, __ATOMIC_ACQUIRE);
        if (result->expected_cause == detail::cause_rxq0) {
            if (udp_stress_phase) {
                __atomic_add_fetch(&detail::info.stress.rx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else if (udp_phase) {
                __atomic_add_fetch(&detail::info.udp.rx_interrupts, 1, __ATOMIC_RELAXED);
            } else if (protocol_phase) {
                __atomic_add_fetch(&detail::info.protocol.rx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else {
                __atomic_add_fetch(&detail::info.packet.rx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            }
        } else if (result->expected_cause == detail::cause_txq0) {
            if (udp_stress_phase) {
                __atomic_add_fetch(&detail::info.stress.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else if (udp_phase) {
                __atomic_add_fetch(&detail::info.udp.tx_interrupts, 1, __ATOMIC_RELAXED);
            } else if (socket_tx_wait_phase) {
                __atomic_add_fetch(&detail::info.socket_tx_wait.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else if (socket_tx_bp_phase) {
                __atomic_add_fetch(&detail::info.socket_tx_bp.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else if (socket_tx_phase) {
                __atomic_add_fetch(&detail::info.socket_tx.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else if (protocol_phase) {
                __atomic_add_fetch(&detail::info.protocol.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            } else {
                __atomic_add_fetch(&detail::info.packet.tx_interrupts, 1,
                                   __ATOMIC_RELAXED);
            }
        }
    }
    detail::mmio_write(detail::reg_icr, result->expected_cause |
                            (packet_phase ? (payload & detail::auxiliary_causes) : 0));
}

bool wait_for_matched(uint32_t index, uint64_t first_tick) {
    while (__atomic_load_n(&detail::info.vectors[index].matched_calls,
                           __ATOMIC_ACQUIRE) == 0) {
        if (scheduler::ticks() - first_tick > detail::wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    return true;
}

bool settle_throttle(uint64_t first_tick) {
    const uint64_t start = scheduler::ticks();
    while (scheduler::ticks() - start < 2) {
        if (scheduler::ticks() - first_tick > detail::wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    return true;
}

bool wait_for_pending(volatile const uint8_t* pba, uint32_t entry,
                      uint64_t first_tick) {
    while (!msix::pending(pba, entry)) {
        if (scheduler::ticks() - first_tick > detail::wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    return true;
}

void set_function_mask(const pci::Device& device, uint8_t capability,
                       bool masked) {
    uint16_t control = pci::config_read16(device, capability + 2);
    control &= ~msix_function_mask;
    if (masked) {
        control |= msix_function_mask;
    }
    pci::config_write16(device, capability + 2, control);
}

bool valid_bar(uint32_t bar) {
    return (bar & 1) == 0 && (bar & 6) == 0 &&
           (bar & 0xfffffff0u) != 0;
}

}  // namespace

namespace detail {

TestInfo info;

volatile bool packet_active = false;
volatile bool socket_tx_active = false;
volatile bool socket_tx_bp_active = false;
volatile bool socket_tx_wait_active = false;
volatile bool protocol_active = false;
volatile bool udp_active = false;
volatile bool udp_stress_active = false;
uint64_t packet_pages[descriptor_count + 2] = {};
size_t packet_page_count = 0;
uint64_t packet_free_before = 0;
bool packet_allocation_attempted = false;

uint32_t mmio_read(uint32_t offset) {
    const auto* address = reinterpret_cast<volatile const uint32_t*>(
        static_cast<uintptr_t>(csr_virtual_base + offset));
    const uint32_t value = *address;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return value;
}

void mmio_write(uint32_t offset, uint32_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    auto* address = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(csr_virtual_base + offset));
    *address = value;
}

uint64_t allocate_packet_page() {
    if (packet_page_count >= sizeof(packet_pages) / sizeof(packet_pages[0])) {
        return 0;
    }
    const uint64_t physical = pmm::allocate_pages_below(1, packet_dma_limit);
    if (physical != 0) {
        packet_pages[packet_page_count++] = physical;
    }
    return physical;
}

void release_packet_pages() {
    while (packet_page_count != 0) {
        const uint64_t physical = packet_pages[--packet_page_count];
        pmm::free_pages(physical, 1);
        packet_pages[packet_page_count] = 0;
    }
}

bool peer_mac_matches(const uint8_t* address) {
    for (size_t index = 0; index < 6; ++index) {
        if (address[index] != peer_mac[index]) {
            return false;
        }
    }
    return true;
}

bool prepare_packet_rings(volatile LegacyRxDescriptor*& rx_ring,
                          uint8_t** rx_buffers,
                          volatile LegacyTxDescriptor*& tx_ring,
                          uint8_t*& tx_buffer) {
    rx_ring = nullptr;
    tx_ring = nullptr;
    tx_buffer = nullptr;
    packet_free_before = pmm::free_pages_count();
    packet_page_count = 0;
    packet_allocation_attempted = true;

    const uint64_t rx_ring_physical = allocate_packet_page();
    const uint64_t tx_ring_physical = allocate_packet_page();
    if (rx_ring_physical == 0 || tx_ring_physical == 0) {
        return false;
    }
    memset(pmm::physical_to_virtual(rx_ring_physical), 0, 4096);
    memset(pmm::physical_to_virtual(tx_ring_physical), 0, 4096);
    rx_ring = static_cast<volatile LegacyRxDescriptor*>(
        pmm::physical_to_virtual(rx_ring_physical));
    tx_ring = static_cast<volatile LegacyTxDescriptor*>(
        pmm::physical_to_virtual(tx_ring_physical));

    for (size_t index = 0; index < rx_buffer_count; ++index) {
        const uint64_t physical = allocate_packet_page();
        if (physical == 0) {
            return false;
        }
        rx_buffers[index] = static_cast<uint8_t*>(pmm::physical_to_virtual(physical));
        memset(rx_buffers[index], 0xa5, 128);
        rx_ring[index].buffer_address = physical;
    }
    const uint64_t tx_buffer_physical = allocate_packet_page();
    if (tx_buffer_physical == 0) {
        return false;
    }
    tx_buffer = static_cast<uint8_t*>(pmm::physical_to_virtual(tx_buffer_physical));

    const uint32_t ral = mmio_read(reg_ral0);
    const uint32_t rah = mmio_read(reg_rah0);
    if ((rah & rah_address_valid) == 0) {
        return false;
    }
    for (size_t index = 0; index < 4; ++index) {
        info.packet.mac[index] = static_cast<uint8_t>(ral >> (index * 8));
    }
    info.packet.mac[4] = static_cast<uint8_t>(rah);
    info.packet.mac[5] = static_cast<uint8_t>(rah >> 8);

    mmio_write(reg_rctl, 0);
    mmio_write(reg_tctl, 0);
    mmio_write(reg_ctrl, mmio_read(reg_ctrl) | ctrl_slu);
    if ((mmio_read(reg_status) & status_link_up) == 0) {
        return false;
    }

    // Single queue, legacy descriptors, no multicast hash or checksum/RSS
    // dependency. The broadcast test frame needs only RCTL.BAM filtering.
    mmio_write(reg_mrqc, 0);
    mmio_write(reg_rfctl, 0);
    mmio_write(reg_rxcsum, 0);
    mmio_write(reg_srrctl0, 0);
    for (uint32_t index = 0; index < 128; ++index) {
        mmio_write(reg_mta + index * 4, 0);
    }

    mmio_write(reg_rdbal0, static_cast<uint32_t>(rx_ring_physical));
    mmio_write(reg_rdbah0, static_cast<uint32_t>(rx_ring_physical >> 32));
    mmio_write(reg_rdlen0, descriptor_bytes);
    mmio_write(reg_rdh0, 0);
    mmio_write(reg_rdt0, 0);
    mmio_write(reg_tdbal0, static_cast<uint32_t>(tx_ring_physical));
    mmio_write(reg_tdbah0, static_cast<uint32_t>(tx_ring_physical >> 32));
    mmio_write(reg_tdlen0, descriptor_bytes);
    mmio_write(reg_tdh0, 0);
    mmio_write(reg_tdt0, 0);

    mmio_write(reg_rdtr0, 0);
    mmio_write(reg_radv0, 0);
    mmio_write(reg_tidv0, 0);
    mmio_write(reg_tadv0, 0);
    mmio_write(reg_rxdctl0, queue_enable | descriptor_granularity |
                                (4u << 16) | (4u << 8) | 0x20u);
    mmio_write(reg_txdctl0, queue_enable | descriptor_granularity |
                                txdctl_count_desc | (1u << 16) | (1u << 8) |
                                0x1fu);
    mmio_write(reg_tarc0, mmio_read(reg_tarc0) | tarc_enable |
                              tarc_82574_workaround);
    mmio_write(reg_tipg, 8u | (2u << 10) | (10u << 20));

    // Initially publish seven RX buffers and leave descriptor 7 empty. RDT=7
    // is one beyond descriptor 6; M13 later recycles pages through every slot.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write(reg_rdt0, rx_buffer_count);
    mmio_write(reg_tctl, tctl_enable | tctl_pad_short | tctl_retransmit_late |
                             (15u << 4) | (64u << 12));
    mmio_write(reg_rctl, rctl_enable | rctl_broadcast | rctl_strip_crc);

    return mmio_read(reg_rdlen0) == descriptor_bytes &&
           mmio_read(reg_tdlen0) == descriptor_bytes &&
           mmio_read(reg_rdt0) == rx_buffer_count &&
           mmio_read(reg_rdh0) == 0 && mmio_read(reg_tdh0) == 0;
}

bool valid_request_frame(const uint8_t* frame, uint16_t length) {
    constexpr uint8_t magic[8] = {'C', 'X', 'C', '1', '1', 'R', 'E', 'Q'};
    if (length != packet_bytes || frame == nullptr) {
        return false;
    }
    for (size_t index = 0; index < 6; ++index) {
        if (frame[index] != 0xff) {
            return false;
        }
    }
    if (!peer_mac_matches(frame + 6) || frame[12] != 0x88 || frame[13] != 0xb5) {
        return false;
    }
    for (size_t index = 0; index < 8; ++index) {
        if (frame[14 + index] != magic[index]) {
            return false;
        }
    }
    for (size_t index = 8; index < 46; ++index) {
        const uint8_t expected = static_cast<uint8_t>(index * 7 + 0x2b);
        if (frame[14 + index] != expected) {
            return false;
        }
    }
    return true;
}

net::Endpoint test_endpoint() {
    return net::Endpoint{info.packet.mac, peer_mac, nic_ipv4, peer_ipv4};
}

void fill_rx_guard(uint8_t* buffer) {
    memset(buffer, 0xa5, 128);
}

bool rx_guard_intact(const uint8_t* buffer, uint16_t length) {
    const size_t guard_start = (static_cast<size_t>(length) + 15) & ~size_t{15};
    if (guard_start > 128) {
        return false;
    }
    for (size_t index = guard_start; index < 128; ++index) {
        if (buffer[index] != 0xa5) {
            return false;
        }
    }
    return true;
}

bool valid_arp_request(const uint8_t* frame, uint16_t length) {
    return net::valid_arp_request(frame, length, test_endpoint());
}

uint16_t build_arp_reply(uint8_t* tx_buffer) {
    return net::build_arp_reply(tx_buffer, test_endpoint());
}

bool valid_icmp_echo_request(const uint8_t* frame, uint16_t length) {
    return net::valid_icmp_echo_request(frame, length, test_endpoint());
}

uint16_t build_icmp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                               uint16_t length) {
    return net::build_icmp_echo_reply(tx_buffer, request, length,
                                      test_endpoint());
}

bool valid_udp_request(const uint8_t* frame, uint16_t length) {
    return net::valid_udp_request(frame, length, test_endpoint());
}

uint16_t build_udp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                              uint16_t request_length) {
    return net::build_udp_echo_reply(tx_buffer, request, request_length,
                                     test_endpoint());
}

uint64_t network_rx_interrupts(NetworkPhase phase) {
    if (phase == NetworkPhase::UdpStress) {
        return __atomic_load_n(&info.stress.rx_interrupts, __ATOMIC_ACQUIRE);
    }
    if (phase == NetworkPhase::Udp) {
        return __atomic_load_n(&info.udp.rx_interrupts, __ATOMIC_ACQUIRE);
    }
    return phase == NetworkPhase::Protocol
               ? __atomic_load_n(&info.protocol.rx_interrupts, __ATOMIC_ACQUIRE)
               : __atomic_load_n(&info.packet.rx_interrupts, __ATOMIC_ACQUIRE);
}

uint64_t network_tx_interrupts(NetworkPhase phase) {
    if (phase == NetworkPhase::UdpStress) {
        return __atomic_load_n(&info.stress.tx_interrupts, __ATOMIC_ACQUIRE);
    }
    if (phase == NetworkPhase::Udp) {
        return __atomic_load_n(&info.udp.tx_interrupts, __ATOMIC_ACQUIRE);
    }
    if (phase == NetworkPhase::UdpSocketTx) {
        return __atomic_load_n(&info.socket_tx.tx_interrupts, __ATOMIC_ACQUIRE);
    }
    if (phase == NetworkPhase::UdpSocketTxBp) {
        return __atomic_load_n(&info.socket_tx_bp.tx_interrupts, __ATOMIC_ACQUIRE);
    }
    if (phase == NetworkPhase::UdpSocketTxWait) {
        return __atomic_load_n(&info.socket_tx_wait.tx_interrupts, __ATOMIC_ACQUIRE);
    }
    return phase == NetworkPhase::Protocol
               ? __atomic_load_n(&info.protocol.tx_interrupts, __ATOMIC_ACQUIRE)
               : __atomic_load_n(&info.packet.tx_interrupts, __ATOMIC_ACQUIRE);
}

bool wait_for_packet_rx(volatile LegacyRxDescriptor& descriptor,
                        uint64_t first_tick, NetworkPhase phase,
                        uint64_t expected_interrupts) {
    while ((descriptor.status & descriptor_done) == 0 ||
           network_rx_interrupts(phase) < expected_interrupts) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return true;
}

bool wait_for_packet_tx(volatile LegacyTxDescriptor& descriptor,
                        uint64_t first_tick, NetworkPhase phase,
                        uint64_t expected_interrupts) {
    while ((descriptor.status & descriptor_done) == 0 ||
           network_tx_interrupts(phase) < expected_interrupts) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return true;
}

bool network_vector_errors() {
    for (const VectorResult& result : info.vectors) {
        if (__atomic_load_n(&result.errors, __ATOMIC_ACQUIRE) != 0) {
            return true;
        }
    }
    return false;
}

bool try_post_tx_frame(volatile LegacyTxDescriptor* tx_ring, uint32_t index,
                       uint16_t length) {
    if (tx_ring == nullptr || index >= descriptor_count || length == 0) {
        return false;
    }
    const uint32_t tdh = mmio_read(reg_tdh0);
    const uint32_t tdt = mmio_read(reg_tdt0);
    if (tdt != index || net::tx_ring_full(tdh, tdt)) {
        return false;
    }
    volatile LegacyTxDescriptor& descriptor = tx_ring[index];
    descriptor.buffer_address = packet_pages[packet_page_count - 1];
    descriptor.length = length;
    descriptor.checksum_offset = 0;
    descriptor.command = tx_command_eop | tx_command_ifcs | tx_command_rs;
    descriptor.status = 0;
    descriptor.checksum_start = 0;
    descriptor.special = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write(reg_tdt0,
               static_cast<uint32_t>((index + 1) % descriptor_count));
    return true;
}

bool post_tx_wait(volatile LegacyTxDescriptor* tx_ring, uint16_t length,
                  uint64_t first_tick, uint32_t& blocked) {
    blocked = 0;
    while (true) {
        const uint32_t tdt = mmio_read(reg_tdt0);
        if (try_post_tx_frame(tx_ring, tdt, length)) {
            return true;
        }
        blocked = 1;
        if (scheduler::ticks() - first_tick > wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
}

bool submit_tx_frame(volatile LegacyTxDescriptor* tx_ring, uint32_t index,
                     uint16_t length, uint64_t first_tick, NetworkPhase phase,
                     uint64_t expected_interrupts) {
    if (!try_post_tx_frame(tx_ring, index, length)) {
        return false;
    }
    return wait_for_packet_tx(tx_ring[index], first_tick, phase,
                              expected_interrupts);
}

}  // namespace detail

bool run_msix_test() {
    if (cpu::interrupts_enabled() || !apic::initialized() ||
        apic::apic_id() > 0xff) {
        return false;
    }

    detail::info = TestInfo{};
    __atomic_store_n(&detail::packet_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::protocol_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::udp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::udp_stress_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::socket_tx_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::socket_tx_bp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&detail::socket_tx_wait_active, false, __ATOMIC_RELEASE);
    detail::packet_allocation_attempted = false;
    detail::packet_page_count = 0;
    detail::packet_free_before = 0;
    const uint64_t unhandled_before = irq::unhandled_count();
    const pci::Device* device = nullptr;
    uint8_t capability = 0;
    uint8_t msi_capability = 0;
    uint16_t original_command = 0;
    uint16_t original_control = 0;
    volatile msix::TableEntry* entries[3] = {};
    bool command_changed = false;
    bool control_read = false;
    bool device_ready = false;
    bool timer_started = false;
    uint32_t registered_count = 0;
    bool success = false;

    do {
        for (size_t index = 0; index < pci::device_count(); ++index) {
            const pci::Device& candidate = pci::devices()[index];
            if (candidate.vendor_id == intel_vendor &&
                candidate.device_id == device_82574l) {
                device = &candidate;
                break;
            }
        }
        if (device == nullptr) {
            break;
        }
        capability = pci::find_capability(*device, 0x11);
        msi_capability = pci::find_capability(*device, 0x05);
        if (capability == 0 || device->msix_table_entries != 5 ||
            device->msix_table_bir != 3 || device->msix_pba_bir != 3) {
            break;
        }
        detail::info.table_entries = device->msix_table_entries;
        detail::info.stage = 1;

        const uint32_t csr_bar = pci::read_bar(*device, 0);
        const uint32_t msix_bar = pci::read_bar(*device, 3);
        if (!valid_bar(csr_bar) || !valid_bar(msix_bar)) {
            break;
        }
        const uint64_t csr_physical = csr_bar & 0xfffffff0u;
        const uint64_t msix_physical = msix_bar & 0xfffffff0u;
        detail::info.csr_physical = csr_physical;
        detail::info.msix_physical = msix_physical;

        const uint64_t table_end = device->msix_table_offset +
                                   device->msix_table_entries * 16;
        const uint64_t pba_end = device->msix_pba_offset + 8;
        const uint64_t mapping_end = table_end > pba_end ? table_end : pba_end;
        if (mapping_end > maximum_msix_mapping) {
            break;
        }
        bool mapping_failed = false;
        for (uint64_t offset = 0; offset < csr_mapping_bytes; offset += 4096) {
            const uint64_t virtual_address = detail::csr_virtual_base + offset;
            if (vmm::is_mapped(virtual_address) ||
                !vmm::map_device_page(csr_physical + offset, virtual_address)) {
                mapping_failed = true;
                break;
            }
        }
        for (uint64_t offset = 0; offset < mapping_end; offset += 4096) {
            const uint64_t virtual_address = msix_virtual_base + offset;
            if (vmm::is_mapped(virtual_address) ||
                !vmm::map_device_page(msix_physical + offset,
                                      virtual_address)) {
                mapping_failed = true;
                break;
            }
        }
        if (mapping_failed) {
            break;
        }
        detail::info.stage = 2;

        original_command = pci::config_read16(*device, 0x04);
        pci::config_write16(*device, 0x04,
                            original_command | command_memory |
                                command_bus_master | command_intx_disable);
        command_changed = true;
        device_ready = true;
        if (msi_capability != 0) {
            const uint16_t msi_control =
                pci::config_read16(*device, msi_capability + 2);
            pci::config_write16(*device, msi_capability + 2,
                                msi_control & ~msi_enable);
        }

        // Quiesce all device causes before publishing any MSI-X entry.
        detail::mmio_write(reg_imc, 0xffffffffu);
        (void)detail::mmio_read(detail::reg_icr);
        detail::mmio_write(reg_eiac, 0);
        detail::mmio_write(reg_iam, 0);
        for (uint32_t index = 0; index < device->msix_table_entries; ++index) {
            // Request no throttling. QEMU clamps this to its 500 minimum and
            // may emit an empty timer notification; the ISR and settle path
            // below handle that model behavior explicitly.
            detail::mmio_write(reg_eitr + index * 4, 0);
        }
        detail::mmio_write(reg_ivar, 0);
        detail::info.stage = 3;

        original_control = pci::config_read16(*device, capability + 2);
        control_read = true;
        pci::config_write16(*device, capability + 2,
                            (original_control & ~msix_enable) |
                                msix_function_mask);

        detail::info.vectors[0].table_entry = 0;
        detail::info.vectors[0].expected_cause = detail::cause_rxq0;
        detail::info.vectors[1].table_entry = 1;
        detail::info.vectors[1].expected_cause = detail::cause_txq0;
        detail::info.vectors[2].table_entry = 4;
        detail::info.vectors[2].expected_cause = cause_rxq1;
        for (uint32_t index = 0; index < 3; ++index) {
            contexts[index].result = &detail::info.vectors[index];
            uint8_t vector = 0;
            if (!irq::register_message(interrupt_handler, &contexts[index],
                                       vector)) {
                break;
            }
            detail::info.vectors[index].idt_vector = vector;
            ++registered_count;
            entries[index] = msix::table_entry(
                reinterpret_cast<volatile uint8_t*>(
                    static_cast<uintptr_t>(msix_virtual_base)),
                device->msix_table_offset, detail::info.vectors[index].table_entry);
            if (!msix::program_entry(entries[index], apic::apic_id(), vector,
                                     true)) {
                break;
            }
        }
        if (registered_count != 3 || entries[0] == nullptr ||
            entries[1] == nullptr || entries[2] == nullptr) {
            break;
        }
        detail::info.stage = 4;

        // RXQ0 -> entry 0, TXQ0 -> entry 1, RXQ1 -> entry 4. Each nibble
        // uses bits 2:0 as the table index and bit 3 as valid.
        const uint32_t ivar = (ivar_valid | 0) |
                              ((ivar_valid | 4) << 4) |
                              ((ivar_valid | 1) << 8);
        detail::mmio_write(reg_ivar, ivar);
        auto* pba = reinterpret_cast<volatile const uint8_t*>(
            static_cast<uintptr_t>(msix_virtual_base +
                                   device->msix_pba_offset));
        if (msix::pending(pba, 0) || msix::pending(pba, 1) ||
            msix::pending(pba, 4)) {
            break;
        }

        pci::config_write16(*device, capability + 2,
                            (original_control & ~msix_function_mask) |
                                msix_enable | msix_function_mask);
        detail::mmio_write(reg_ivar, ivar);
        // IMS is a set-only register; IMC above already cleared every bit.
        detail::mmio_write(reg_ims, tested_causes);
        detail::info.stage = 5;

        apic::start_periodic();
        timer_started = true;
        const uint64_t first_tick = scheduler::ticks();
        cpu::enable_interrupts();
        set_function_mask(*device, capability, false);

        // Entry 0: ordinary direct MSI-X delivery.
        msix::set_masked(entries[0], false);
        detail::mmio_write(detail::reg_ics, detail::cause_rxq0);
        if (!wait_for_matched(0, first_tick) ||
            __atomic_load_n(&detail::info.vectors[0].matched_calls,
                            __ATOMIC_ACQUIRE) != 1 ||
            !settle_throttle(first_tick)) {
            break;
        }
        detail::info.stage = 6;

        // Entry 1 stays masked while TXQ0 is raised. PBA must latch the
        // pending message without calling the IDT handler; unmask delivers it.
        detail::mmio_write(detail::reg_ics, detail::cause_txq0);
        if (!wait_for_pending(pba, 1, first_tick) ||
            __atomic_load_n(&detail::info.vectors[1].calls, __ATOMIC_ACQUIRE) != 0) {
            break;
        }
        detail::info.per_vector_pba_seen = 1;
        detail::info.stage = 7;
        msix::set_masked(entries[1], false);
        if (!wait_for_matched(1, first_tick) || msix::pending(pba, 1) ||
            __atomic_load_n(&detail::info.vectors[1].matched_calls,
                            __ATOMIC_ACQUIRE) != 1 ||
            !settle_throttle(first_tick)) {
            break;
        }
        detail::info.stage = 8;

        // Entry 4 is unmasked, but the MSI-X function mask blocks delivery.
        msix::set_masked(entries[2], false);
        set_function_mask(*device, capability, true);
        detail::mmio_write(detail::reg_ics, cause_rxq1);
        if (!wait_for_pending(pba, 4, first_tick) ||
            __atomic_load_n(&detail::info.vectors[2].calls, __ATOMIC_ACQUIRE) != 0) {
            break;
        }
        detail::info.function_pba_seen = 1;
        detail::info.stage = 9;
        set_function_mask(*device, capability, false);
        if (!wait_for_matched(2, first_tick) || msix::pending(pba, 4) ||
            __atomic_load_n(&detail::info.vectors[2].matched_calls,
                            __ATOMIC_ACQUIRE) != 1 ||
            !settle_throttle(first_tick)) {
            break;
        }
        detail::info.stage = 10;

        cpu::disable_interrupts();
        apic::stop_periodic();
        timer_started = false;
        success = detail::info.per_vector_pba_seen == 1 &&
                  detail::info.function_pba_seen == 1 && !msix::pending(pba, 0) &&
                  !msix::pending(pba, 1) && !msix::pending(pba, 4) &&
                  (detail::mmio_read(detail::reg_ics) & ~detail::icr_asserted) == 0;
        for (const VectorResult& result : detail::info.vectors) {
            if (__atomic_load_n(&result.matched_calls, __ATOMIC_ACQUIRE) != 1 ||
                __atomic_load_n(&result.errors, __ATOMIC_ACQUIRE) != 0 ||
                (__atomic_load_n(&result.last_cause, __ATOMIC_ACQUIRE) &
                 result.expected_cause) == 0) {
                success = false;
            }
        }
        if (success) {
            detail::info.stage = 11;
            detail::info.stage = 20;
            if (detail::run_packet_phase()) {
                detail::info.stage = 21;
            } else {
                success = false;
            }
        }
    } while (false);

    cpu::disable_interrupts();
    if (timer_started) {
        apic::stop_periodic();
    }
    if (device_ready) {
        detail::mmio_write(reg_imc, 0xffffffffu);
        (void)detail::mmio_read(detail::reg_icr);
        detail::mmio_write(reg_eiac, 0);
        detail::mmio_write(reg_iam, 0);
        detail::mmio_write(reg_ivar, 0);
    }
    if (control_read) {
        const uint16_t control = pci::config_read16(*device, capability + 2);
        pci::config_write16(*device, capability + 2,
                            (control & ~msix_enable) | msix_function_mask);
    }
    for (volatile msix::TableEntry* entry : entries) {
        if (entry != nullptr) {
            msix::set_masked(entry, true);
        }
    }

    bool cleanup_ok = true;
    if (command_changed) {
        pci::config_write16(*device, 0x04,
                            (original_command & ~command_bus_master) |
                                command_intx_disable);
    }
    if (detail::packet_allocation_attempted) {
        detail::release_packet_pages();
        if (pmm::free_pages_count() != detail::packet_free_before) {
            cleanup_ok = false;
        }
    }
    for (uint32_t index = 0; index < registered_count; ++index) {
        if (!irq::unregister(static_cast<uint8_t>(detail::info.vectors[index].idt_vector))) {
            cleanup_ok = false;
        }
    }

    return success && cleanup_ok && irq::unhandled_count() == unhandled_before;
}

const TestInfo& test_info() {
    return detail::info;
}

}  // namespace cxc::e1000e
