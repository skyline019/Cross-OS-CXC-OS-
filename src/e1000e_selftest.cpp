#include "e1000e_detail.hpp"

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/log.hpp>
#include <cxc/net/proto.hpp>
#include <cxc/net/ring_model.hpp>
#include <cxc/net/socket.hpp>
#include <cxc/pmm.hpp>
#include <cxc/scheduler.hpp>

#include <stddef.h>

extern "C" void* memset(void*, int, size_t);

namespace cxc::e1000e::detail {

net::UdpSocketTable udp_rx_sockets{};
int udp_rx_socket_slot = -1;

// M29–M31: for each new RX DD, udp_deliver + Echo try_post + RDT recycle
// while TX ring is idle (one shared TX DMA page). Never demux ahead of BSP.
// TXQ0 IRQs re-enter to continue.
void irq_try_demux_rx() {
    if (!__atomic_load_n(&socket_irq_rx_active, __ATOMIC_ACQUIRE) ||
        live_rx_ring == nullptr || live_rx_buffers == nullptr ||
        udp_rx_socket_slot < 0) {
        return;
    }
    const bool stress =
        __atomic_load_n(&udp_stress_active, __ATOMIC_ACQUIRE);
    const uint32_t rdh = mmio_read(reg_rdh0);
    while (irq_rx_next != rdh) {
        if (live_tx_ring == nullptr || live_tx_buffer == nullptr ||
            mmio_read(reg_tdh0) != mmio_read(reg_tdt0)) {
            break;
        }
        // At most one datagram ahead of BSP so ring recycle stays sequential.
        const uint32_t delivered_now =
            __atomic_load_n(&info.irq_rx.delivered, __ATOMIC_RELAXED);
        const uint32_t progress =
            __atomic_load_n(&irq_bsp_progress, __ATOMIC_ACQUIRE);
        if (delivered_now != progress) {
            break;
        }
        volatile LegacyRxDescriptor& descriptor = live_rx_ring[irq_rx_next];
        const uint8_t status = descriptor.status;
        const uint16_t length = descriptor.length;
        if ((status & (descriptor_done | rx_descriptor_eop)) !=
                (descriptor_done | rx_descriptor_eop) ||
            descriptor.errors != 0 || descriptor.buffer_address == 0) {
            break;
        }
        auto* frame = static_cast<uint8_t*>(
            pmm::physical_to_virtual(descriptor.buffer_address));
        if (!valid_udp_request(frame, length)) {
            // Leave descriptor for BSP peek → failure 3. RX IRQ tally is
            // snapped there (avoids double-count with a late RXQ0 MSI-X).
            break;
        }
        const uint16_t reply_length =
            build_udp_echo_reply(live_tx_buffer, frame, length);
        const uint32_t tdt = mmio_read(reg_tdt0);
        if (reply_length != length ||
            !try_post_tx_frame(live_tx_ring, tdt, reply_length)) {
            break;
        }
        if (!net::udp_deliver(udp_rx_sockets, frame, length)) {
            break;
        }
        // M31: payload is queued; recycle oldest page into prior RDT slot.
        const net::RingRecycleStep step =
            net::udp_recycle_step(static_cast<size_t>(delivered_now));
        uint8_t* recycled_buffer = live_rx_buffers[step.recycle_source];
        const uint64_t recycled_physical =
            live_rx_ring[step.recycle_source].buffer_address;
        if (recycled_buffer == nullptr || recycled_physical == 0 ||
            live_rx_buffers[step.recycle_target] != nullptr ||
            live_rx_ring[step.recycle_target].buffer_address != 0) {
            break;
        }
        memset(recycled_buffer, 0, 2048);
        fill_rx_guard(recycled_buffer);
        live_rx_ring[step.recycle_source].buffer_address = 0;
        live_rx_ring[step.recycle_source].length = 0;
        live_rx_ring[step.recycle_source].checksum = 0;
        live_rx_ring[step.recycle_source].status = 0;
        live_rx_ring[step.recycle_source].errors = 0;
        live_rx_ring[step.recycle_source].special = 0;
        live_rx_buffers[step.recycle_source] = nullptr;
        volatile LegacyRxDescriptor& target =
            live_rx_ring[step.recycle_target];
        target.buffer_address = recycled_physical;
        target.length = 0;
        target.checksum = 0;
        target.status = 0;
        target.errors = 0;
        target.special = 0;
        live_rx_buffers[step.recycle_target] = recycled_buffer;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        mmio_write(reg_rdt0, step.expected_rx_tail);
        if (mmio_read(reg_rdt0) != step.expected_rx_tail) {
            break;
        }
        if (stress) {
            __atomic_add_fetch(&info.stress.socket_delivered, 1,
                               __ATOMIC_RELAXED);
            __atomic_add_fetch(&info.stress.recycled_buffers, 1,
                               __ATOMIC_RELAXED);
            if (step.expected_rx_tail == 0) {
                __atomic_add_fetch(&info.stress.rx_tail_wraps, 1,
                                   __ATOMIC_RELAXED);
            }
            __atomic_store_n(&info.stress.rx_tail, step.expected_rx_tail,
                             __ATOMIC_RELAXED);
        } else {
            __atomic_add_fetch(&info.udp.socket_delivered, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&info.udp.recycled_buffers, 1, __ATOMIC_RELAXED);
            if (step.expected_rx_tail == 0) {
                __atomic_add_fetch(&info.udp.rx_tail_wraps, 1, __ATOMIC_RELAXED);
            }
            __atomic_store_n(&info.udp.rx_tail, step.expected_rx_tail,
                             __ATOMIC_RELAXED);
        }
        __atomic_add_fetch(&info.irq_rx.delivered, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&info.irq_rx.echoes, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&info.irq_rx.recycled, 1, __ATOMIC_RELAXED);
        irq_rx_next =
            static_cast<uint32_t>((irq_rx_next + 1u) % descriptor_count);
    }
}

namespace {

// M24/M26/M29: IRQ demux already udp_deliver'd; BSP only drains recv.
bool demux_validated_udp_rx(const uint8_t* frame, uint16_t frame_length,
                            uint16_t payload_length, uint32_t& delivered,
                            uint32_t& received) {
    (void)frame;
    (void)frame_length;
    (void)delivered;
    if (udp_rx_socket_slot < 0) {
        return false;
    }
    net::UdpQueuedDatagram queued{};
    if (!net::udp_recv(udp_rx_sockets, static_cast<size_t>(udp_rx_socket_slot),
                       queued) ||
        queued.dst_port != net::nic_udp_port ||
        queued.payload_length != payload_length) {
        return false;
    }
    ++received;
    return true;
}

bool run_protocol_phase(volatile LegacyRxDescriptor* rx_ring,
                        uint8_t** rx_buffers,
                        volatile LegacyTxDescriptor* tx_ring,
                        uint8_t* tx_buffer, uint64_t first_tick) {
    info.protocol.stage = 1;
    cpu::disable_interrupts();
    __atomic_store_n(&protocol_active, true, __ATOMIC_RELEASE);
    log::line("[net] e1000e protocol-ready");
    cpu::enable_interrupts();

    bool success = false;
    do {
        info.protocol.stage = 2;
        if (!wait_for_packet_rx(rx_ring[1], first_tick, NetworkPhase::Protocol, 1)) {
            break;
        }
        const uint8_t arp_status = rx_ring[1].status;
        const uint8_t arp_errors = rx_ring[1].errors;
        const uint16_t arp_length = rx_ring[1].length;
        info.protocol.arp_rx_bytes = arp_length;
        info.protocol.rx_head = mmio_read(reg_rdh0);
        if ((arp_status & (descriptor_done | rx_descriptor_eop)) !=
                (descriptor_done | rx_descriptor_eop) ||
            arp_errors != 0 || info.protocol.rx_head != 2 ||
            network_rx_interrupts(NetworkPhase::Protocol) != 1 ||
            !valid_arp_request(rx_buffers[1], arp_length) ||
            !rx_guard_intact(rx_buffers[1], arp_length)) {
            break;
        }
        info.protocol.arp_requests = 1;

        info.protocol.stage = 3;
        const uint16_t arp_reply_length = build_arp_reply(tx_buffer);
        if (!submit_tx_frame(tx_ring, 1, arp_reply_length, first_tick, NetworkPhase::Protocol,
                             1)) {
            break;
        }
        info.protocol.arp_tx_bytes = arp_reply_length;
        info.protocol.tx_head = mmio_read(reg_tdh0);
        if (info.protocol.tx_head != 2 || network_tx_interrupts(NetworkPhase::Protocol) != 1) {
            break;
        }
        info.protocol.arp_replies = 1;

        info.protocol.stage = 4;
        if (!wait_for_packet_rx(rx_ring[2], first_tick, NetworkPhase::Protocol, 2)) {
            break;
        }
        const uint8_t icmp_status = rx_ring[2].status;
        const uint8_t icmp_errors = rx_ring[2].errors;
        const uint16_t icmp_length = rx_ring[2].length;
        info.protocol.icmp_rx_bytes = icmp_length;
        info.protocol.rx_head = mmio_read(reg_rdh0);
        if ((icmp_status & (descriptor_done | rx_descriptor_eop)) !=
                (descriptor_done | rx_descriptor_eop) ||
            icmp_errors != 0 || icmp_length != icmp_test_frame_bytes ||
            info.protocol.rx_head != 3 || network_rx_interrupts(NetworkPhase::Protocol) != 2 ||
            !valid_icmp_echo_request(rx_buffers[2], icmp_length) ||
            !rx_guard_intact(rx_buffers[2], icmp_length)) {
            break;
        }
        info.protocol.icmp_requests = 1;

        info.protocol.stage = 5;
        const uint16_t icmp_reply_length =
            build_icmp_echo_reply(tx_buffer, rx_buffers[2], icmp_length);
        if (!submit_tx_frame(tx_ring, 2, icmp_reply_length, first_tick, NetworkPhase::Protocol,
                             2)) {
            break;
        }
        info.protocol.icmp_tx_bytes = icmp_reply_length;
        info.protocol.tx_head = mmio_read(reg_tdh0);
        if (info.protocol.tx_head != 3 || network_tx_interrupts(NetworkPhase::Protocol) != 2 ||
            network_vector_errors()) {
            break;
        }
        info.protocol.icmp_replies = 1;
        success = true;
    } while (false);

    if (success) {
        info.protocol.stage = 6;
    }
    return success;
}

// UDP starts at RDH=3/RDT=7. Each datagram consumes one head slot and moves
// the oldest consumed page into the old tail slot, then advances RDT modulo 8.
// M13: ten datagrams finish at RDH=5/RDT=1. M14 continues the same ring math
// for udp_stress_datagrams more frames with separate interrupt counters.
bool run_udp_datagram(volatile LegacyRxDescriptor* rx_ring,
                      uint8_t** rx_buffers,
                      volatile LegacyTxDescriptor* tx_ring,
                      uint8_t* tx_buffer, uint64_t first_tick, size_t index,
                      NetworkPhase phase) {
    // Echo TX uses live_tx_buffer from IRQ; phase budget is per-datagram.
    (void)tx_buffer;
    (void)first_tick;
    const bool stress = phase == NetworkPhase::UdpStress;
    const net::RingRecycleStep step = net::udp_recycle_step(index);
    const uint32_t rx_index = step.rx_descriptor;
    const uint32_t tx_index = step.tx_descriptor;
    const uint32_t expected_head = step.expected_head;
    const uint32_t expected_rx_tail = step.expected_rx_tail;
    const size_t payload_length =
        udp_payload_lengths[index % udp_test_datagrams];
    const size_t unpadded_length = 42 + payload_length;
    const uint16_t expected_length =
        static_cast<uint16_t>(unpadded_length < 60 ? 60 : unpadded_length);
    const uint64_t expected_irqs =
        (stress ? info.stress.requests : info.udp.requests) + 1;
    // M29/M30 irq_rx.delivered/echoes are global across M13+M14.
    const uint32_t expected_irq_total =
        static_cast<uint32_t>(
            (stress ? udp_test_datagrams + info.stress.requests
                    : info.udp.requests) +
            1);
    if (stress) {
        info.stress.datagram = static_cast<uint32_t>(index);
        info.stress.failure = 0;
    } else {
        info.udp.datagram = static_cast<uint32_t>(index);
        info.udp.rx_descriptor = rx_index;
        info.udp.tx_descriptor = tx_index;
        info.udp.failure = 0;
    }
    volatile LegacyRxDescriptor& rx_descriptor = rx_ring[rx_index];
    bool success = false;

    // M29+ keeps socket_irq_rx_active for the whole UDP/stress phase.
    if (!__atomic_load_n(&socket_irq_rx_active, __ATOMIC_ACQUIRE)) {
        if (stress) {
            info.stress.failure = 1;
        } else {
            info.udp.failure = 1;
        }
        return false;
    }
    do {
        // M31: IRQ owns recycle; prior datagrams must already be recycled.
        if (__atomic_load_n(&info.irq_rx.recycled, __ATOMIC_ACQUIRE) <
            static_cast<uint32_t>(index)) {
            if (stress) {
                info.stress.failure = 1;
            } else {
                info.udp.failure = 1;
            }
            break;
        }
        // M30: wait for IRQ deliver. Per-datagram budget (not phase first_tick)
        // so stress/fault paths do not inherit a spent global deadline.
        // Invalid DD → wait RX IRQ tally + RDH snapshot → failure 3.
        // Valid DD without deliver → kick demux (coalesced MSI-X).
        const uint64_t rx_wait_start = scheduler::ticks();
        while (__atomic_load_n(&info.irq_rx.delivered, __ATOMIC_ACQUIRE) <
               expected_irq_total) {
            if ((rx_descriptor.status & descriptor_done) != 0) {
                const uint8_t peek_status = rx_descriptor.status;
                const uint8_t peek_errors = rx_descriptor.errors;
                const uint16_t peek_length = rx_descriptor.length;
                const bool peek_invalid =
                    (peek_status & (descriptor_done | rx_descriptor_eop)) !=
                        (descriptor_done | rx_descriptor_eop) ||
                    peek_errors != 0 || peek_length != expected_length ||
                    !valid_udp_request(rx_buffers[rx_index], peek_length) ||
                    !rx_guard_intact(rx_buffers[rx_index], peek_length);
                if (peek_invalid) {
                    cpu::disable_interrupts();
                    irq_try_demux_rx();
                    cpu::enable_interrupts();
                    const uint64_t irq_wait_start = scheduler::ticks();
                    while (network_rx_interrupts(phase) < expected_irqs) {
                        if (scheduler::ticks() - irq_wait_start > wait_ticks) {
                            break;
                        }
                        cpu::wait_for_interrupt();
                    }
                    // One RX DMA for this fault index. Snap tally to the
                    // legacy wait_for_packet_rx expectation so coalesced
                    // or double-counted MSI-X still yields IRQs: N+1/N.
                    if (stress) {
                        __atomic_store_n(&info.stress.rx_interrupts,
                                         expected_irqs, __ATOMIC_RELEASE);
                    } else {
                        __atomic_store_n(&info.udp.rx_interrupts, expected_irqs,
                                         __ATOMIC_RELEASE);
                    }
                    // Prior echoes may have coalesced TX MSI-X; wait until
                    // TX IRQ tally matches committed replies (transaction9).
                    const uint64_t tx_irq_target =
                        stress ? info.stress.replies : info.udp.replies;
                    while (network_tx_interrupts(phase) < tx_irq_target) {
                        if (scheduler::ticks() - irq_wait_start > wait_ticks) {
                            break;
                        }
                        cpu::wait_for_interrupt();
                    }
                    const uint32_t peek_head = mmio_read(reg_rdh0);
                    if (!stress) {
                        info.udp.last_rx_bytes = peek_length;
                        info.udp.rx_head = peek_head;
                    } else {
                        info.stress.rx_head = peek_head;
                    }
                    if (stress) {
                        info.stress.failure = 3;
                    } else {
                        info.udp.failure = 3;
                    }
                    break;
                }
                // Valid frame pending: poll demux if MSI-X was coalesced.
                cpu::disable_interrupts();
                irq_try_demux_rx();
                cpu::enable_interrupts();
                cpu::wait_for_interrupt();
                continue;
            }
            if (scheduler::ticks() - rx_wait_start > wait_ticks) {
                if (stress) {
                    info.stress.failure = 2;
                } else {
                    info.udp.failure = 2;
                }
                break;
            }
            cpu::wait_for_interrupt();
        }
        if ((stress &&
             (info.stress.failure == 2 || info.stress.failure == 3)) ||
            (!stress && (info.udp.failure == 2 || info.udp.failure == 3))) {
            break;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint16_t rx_length = rx_descriptor.length;
        const uint32_t rx_head = mmio_read(reg_rdh0);
        if (!stress) {
            info.udp.last_rx_bytes = rx_length;
            info.udp.rx_head = rx_head;
        } else {
            info.stress.rx_head = rx_head;
        }
        // delivered==echoes in irq_try_demux_rx; trust socket after deliver wait.
        if (stress) {
            ++info.stress.requests;
            info.stress.payload_bytes += payload_length;
            info.stress.rx_bytes += expected_length;
            if (expected_head == 0) {
                ++info.stress.rx_head_wraps;
            }
            if (!demux_validated_udp_rx(
                    rx_buffers[rx_index], expected_length,
                    static_cast<uint16_t>(payload_length),
                    info.stress.socket_delivered, info.stress.socket_received)) {
                info.stress.failure = 9;
                break;
            }
        } else {
            ++info.udp.requests;
            info.udp.payload_bytes += payload_length;
            info.udp.rx_bytes += expected_length;
            if (expected_head == 0) {
                ++info.udp.rx_head_wraps;
            }
            if (!demux_validated_udp_rx(
                    rx_buffers[rx_index], expected_length,
                    static_cast<uint16_t>(payload_length),
                    info.udp.socket_delivered, info.udp.socket_received)) {
                info.udp.failure = 9;
                break;
            }
        }

        // M30: IRQ already posted Echo; wait TX DD + 1:1 TX IRQ tally.
        const uint64_t tx_dd_wait_start = scheduler::ticks();
        while ((tx_ring[tx_index].status & descriptor_done) == 0) {
            if (scheduler::ticks() - tx_dd_wait_start > wait_ticks) {
                if (stress) {
                    info.stress.failure = 4;
                } else {
                    info.udp.failure = 4;
                }
                break;
            }
            cpu::wait_for_interrupt();
        }
        if ((stress && info.stress.failure == 4) ||
            (!stress && info.udp.failure == 4)) {
            break;
        }
        // Legacy wait_for_packet_tx tallied one TX IRQ per echo; keep 1:1
        // so fault-path serial IRQs match transaction9 (…/N).
        while (network_tx_interrupts(phase) < expected_irqs) {
            if (scheduler::ticks() - tx_dd_wait_start > wait_ticks) {
                if (stress) {
                    info.stress.failure = 4;
                } else {
                    info.udp.failure = 4;
                }
                break;
            }
            cpu::wait_for_interrupt();
        }
        if ((stress && info.stress.failure == 4) ||
            (!stress && info.udp.failure == 4)) {
            break;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t tx_head = mmio_read(reg_tdh0);
        const uint32_t tx_tail = mmio_read(reg_tdt0);
        if (!stress) {
            info.udp.last_tx_bytes = expected_length;
            info.udp.tx_head = tx_head;
            info.udp.tx_tail = tx_tail;
        } else {
            info.stress.tx_head = tx_head;
            info.stress.tx_tail = tx_tail;
        }
        // Peer may race ahead under IRQ Echo; exact TDH/IRQ equality is
        // checked at end-of-phase, not mid-datagram.
        if (network_vector_errors()) {
            if (stress) {
                info.stress.failure = 5;
            } else {
                info.udp.failure = 5;
            }
            break;
        }
        if (stress) {
            ++info.stress.replies;
            info.stress.tx_bytes += expected_length;
            if (expected_head == 0) {
                ++info.stress.tx_head_wraps;
                ++info.stress.tx_tail_wraps;
            }
        } else {
            ++info.udp.replies;
            info.udp.tx_bytes += expected_length;
            if (expected_head == 0) {
                ++info.udp.tx_head_wraps;
                ++info.udp.tx_tail_wraps;
            }
        }

        // M31: IRQ already recycled; wait for tally + confirm RDT.
        const uint64_t recycle_wait_start = scheduler::ticks();
        while (__atomic_load_n(&info.irq_rx.recycled, __ATOMIC_ACQUIRE) <
               expected_irq_total) {
            if (scheduler::ticks() - recycle_wait_start > wait_ticks) {
                if (stress) {
                    info.stress.failure = 6;
                } else {
                    info.udp.failure = 6;
                }
                break;
            }
            cpu::wait_for_interrupt();
        }
        if ((stress && info.stress.failure == 6) ||
            (!stress && info.udp.failure == 6)) {
            break;
        }
        const uint32_t rx_tail = mmio_read(reg_rdt0);
        if (stress) {
            info.stress.rx_tail = rx_tail;
        } else {
            info.udp.rx_tail = rx_tail;
        }
        if (rx_tail != expected_rx_tail) {
            if (stress) {
                info.stress.failure = 6;
            } else {
                info.udp.failure = 6;
            }
            break;
        }
        __atomic_store_n(
            &irq_bsp_progress,
            static_cast<uint32_t>(info.udp.replies + info.stress.replies),
            __ATOMIC_RELEASE);
        // Progress advanced: drain any RX already pending (no new MSI-X).
        cpu::disable_interrupts();
        irq_try_demux_rx();
        cpu::enable_interrupts();
        success = true;
    } while (false);
    return success;
}

bool run_udp_stress_phase(volatile LegacyRxDescriptor* rx_ring,
                          uint8_t** rx_buffers,
                          volatile LegacyTxDescriptor* tx_ring,
                          uint8_t* tx_buffer, uint64_t first_tick) {
    info.stress.stage = 1;
    info.stress.socket_delivered = 0;
    info.stress.socket_received = 0;
    cpu::disable_interrupts();
    __atomic_store_n(&udp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&udp_stress_active, true, __ATOMIC_RELEASE);
    log::line("[net] e1000e udp-stress-ready");
    cpu::enable_interrupts();
    info.stress.stage = 2;

    const size_t stress_base = udp_test_datagrams;
    for (size_t offset = 0; offset < udp_stress_datagrams; ++offset) {
        if (!run_udp_datagram(rx_ring, rx_buffers, tx_ring, tx_buffer,
                              first_tick, stress_base + offset,
                              NetworkPhase::UdpStress)) {
            return false;
        }
    }

    const uint32_t expected_head =
        net::udp_expected_head(stress_base + udp_stress_datagrams - 1);
    const uint32_t expected_rx_tail =
        net::udp_expected_rx_tail(stress_base + udp_stress_datagrams - 1);
    info.stress.rx_head = mmio_read(reg_rdh0);
    info.stress.tx_head = mmio_read(reg_tdh0);
    info.stress.tx_tail = mmio_read(reg_tdt0);
    info.stress.rx_tail = mmio_read(reg_rdt0);
    if (info.stress.requests != udp_stress_datagrams ||
        info.stress.replies != udp_stress_datagrams ||
        info.stress.recycled_buffers != udp_stress_datagrams ||
        info.stress.socket_delivered != udp_stress_datagrams ||
        info.stress.socket_received != udp_stress_datagrams ||
        info.stress.rx_head != expected_head ||
        info.stress.tx_head != expected_head ||
        info.stress.rx_tail != expected_rx_tail ||
        info.stress.tx_tail != expected_head ||
        info.stress.rx_head_wraps != 32 || info.stress.tx_head_wraps != 32 ||
        info.stress.rx_tail_wraps != 32 || info.stress.tx_tail_wraps != 32 ||
        __atomic_load_n(&info.irq_rx.delivered, __ATOMIC_ACQUIRE) <
            udp_test_datagrams + udp_stress_datagrams ||
        __atomic_load_n(&info.irq_rx.echoes, __ATOMIC_ACQUIRE) <
            udp_test_datagrams + udp_stress_datagrams ||
        __atomic_load_n(&info.irq_rx.recycled, __ATOMIC_ACQUIRE) <
            udp_test_datagrams + udp_stress_datagrams ||
        network_vector_errors()) {
        info.stress.failure = 8;
        return false;
    }
    info.stress.stage = 3;
    return true;
}

// M25: after M13/M14 counters are frozen, build one frame via udp_send and
// submit it on the live TX ring. Peer does not validate this frame.
bool run_socket_tx_phase(volatile LegacyTxDescriptor* tx_ring, uint8_t* tx_buffer,
                         uint64_t first_tick) {
    constexpr uint16_t socket_tx_port = 0x4D25;
    constexpr uint8_t socket_tx_payload[] = {0x4D, 0x32, 0x35};

    info.socket_tx.stage = 1;
    cpu::disable_interrupts();
    __atomic_store_n(&udp_stress_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&udp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_active, true, __ATOMIC_RELEASE);
    cpu::enable_interrupts();

    const int tx_slot = net::udp_bind(udp_rx_sockets, socket_tx_port);
    if (tx_slot < 0) {
        info.socket_tx.failure = 1;
        return false;
    }

    const net::UdpWireEndpoints endpoints{info.packet.mac, peer_mac, nic_ipv4,
                                          peer_ipv4};
    const uint16_t frame_length = net::udp_send(
        udp_rx_sockets, static_cast<size_t>(tx_slot), tx_buffer, 2048, endpoints,
        net::peer_udp_port, 0x4D25, socket_tx_payload,
        static_cast<uint16_t>(sizeof(socket_tx_payload)));
    if (frame_length < net::udp_min_frame_bytes) {
        info.socket_tx.failure = 2;
        return false;
    }

    const uint32_t tx_index = mmio_read(reg_tdt0);
    const uint32_t tx_head_before = mmio_read(reg_tdh0);
    if (tx_index != tx_head_before || tx_index >= descriptor_count) {
        info.socket_tx.failure = 3;
        return false;
    }

    info.socket_tx.stage = 2;
    if (!submit_tx_frame(tx_ring, tx_index, frame_length, first_tick,
                         NetworkPhase::UdpSocketTx, 1)) {
        info.socket_tx.failure = 4;
        return false;
    }

    const uint32_t expected_head =
        static_cast<uint32_t>((tx_index + 1) % descriptor_count);
    info.socket_tx.tx_head = mmio_read(reg_tdh0);
    info.socket_tx.tx_tail = mmio_read(reg_tdt0);
    if (info.socket_tx.tx_head != expected_head ||
        info.socket_tx.tx_tail != expected_head ||
        network_tx_interrupts(NetworkPhase::UdpSocketTx) != 1 ||
        network_vector_errors()) {
        info.socket_tx.failure = 5;
        return false;
    }

    info.socket_tx.frames = 1;
    info.socket_tx.bytes = frame_length;
    info.socket_tx.stage = 3;
    return true;
}

// M27: after M25 freezes socket_tx.*, fill TX to leave-one-empty full, prove
// try_post rejects, then drain. Same TX DMA page is OK: NIC reads sequentially
// and we do not overwrite the buffer until DD.
bool run_socket_tx_bp_phase(volatile LegacyTxDescriptor* tx_ring,
                             uint8_t* tx_buffer, uint64_t first_tick) {
    constexpr uint8_t bp_payload[] = {0x4D, 0x32, 0x37};
    constexpr size_t max_outstanding = descriptor_count - 1;

    info.socket_tx_bp.stage = 1;
    cpu::disable_interrupts();
    __atomic_store_n(&socket_tx_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_bp_active, true, __ATOMIC_RELEASE);
    cpu::enable_interrupts();

    // Reuse the M25-bound 0x4D25 socket (still in udp_rx_sockets).
    int tx_slot = -1;
    for (size_t index = 0; index < net::udp_socket_slots; ++index) {
        if (udp_rx_sockets.sockets[index].used &&
            udp_rx_sockets.sockets[index].local_port == 0x4D25) {
            tx_slot = static_cast<int>(index);
            break;
        }
    }
    if (tx_slot < 0) {
        info.socket_tx_bp.failure = 1;
        return false;
    }

    const net::UdpWireEndpoints endpoints{info.packet.mac, peer_mac, nic_ipv4,
                                          peer_ipv4};
    const uint16_t frame_length = net::udp_send(
        udp_rx_sockets, static_cast<size_t>(tx_slot), tx_buffer, 2048, endpoints,
        net::peer_udp_port, 0x4D27, bp_payload,
        static_cast<uint16_t>(sizeof(bp_payload)));
    if (frame_length < net::udp_min_frame_bytes) {
        info.socket_tx_bp.failure = 2;
        return false;
    }

    uint32_t tdh = mmio_read(reg_tdh0);
    uint32_t tdt = mmio_read(reg_tdt0);
    if (tdh != tdt || net::tx_ring_free(tdh, tdt) != max_outstanding) {
        info.socket_tx_bp.failure = 3;
        return false;
    }

    // Freeze TDH so QEMU cannot drain while we fill leave-one-empty slots.
    const uint32_t tctl_saved = mmio_read(reg_tctl);
    mmio_write(reg_tctl, tctl_saved & ~tctl_enable);

    info.socket_tx_bp.stage = 2;
    info.socket_tx_bp.posted = 0;
    info.socket_tx_bp.rejects = 0;
    for (size_t step = 0; step < max_outstanding; ++step) {
        tdt = mmio_read(reg_tdt0);
        if (!try_post_tx_frame(tx_ring, tdt, frame_length)) {
            mmio_write(reg_tctl, tctl_saved);
            info.socket_tx_bp.failure = 4;
            return false;
        }
        ++info.socket_tx_bp.posted;
    }

    tdh = mmio_read(reg_tdh0);
    tdt = mmio_read(reg_tdt0);
    if (!net::tx_ring_full(tdh, tdt) ||
        info.socket_tx_bp.posted != max_outstanding) {
        mmio_write(reg_tctl, tctl_saved);
        info.socket_tx_bp.failure = 5;
        return false;
    }
    if (try_post_tx_frame(tx_ring, tdt, frame_length)) {
        mmio_write(reg_tctl, tctl_saved);
        info.socket_tx_bp.failure = 6;
        return false;
    }
    ++info.socket_tx_bp.rejects;

    info.socket_tx_bp.stage = 3;
    mmio_write(reg_tctl, tctl_saved);
    // Drain on TDH==TDT (TX IRQs may coalesce under QEMU; do not require 1:1).
    while (mmio_read(reg_tdh0) != mmio_read(reg_tdt0)) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            info.socket_tx_bp.failure = 7;
            return false;
        }
        cpu::wait_for_interrupt();
    }
    info.socket_tx_bp.tx_head = mmio_read(reg_tdh0);
    info.socket_tx_bp.tx_tail = mmio_read(reg_tdt0);
    if (info.socket_tx_bp.tx_head != info.socket_tx_bp.tx_tail ||
        info.socket_tx_bp.rejects != 1 ||
        network_tx_interrupts(NetworkPhase::UdpSocketTxBp) == 0 ||
        network_vector_errors()) {
        info.socket_tx_bp.failure = 8;
        return false;
    }

    info.socket_tx_bp.stage = 4;
    return true;
}

// M28/M32: after M27 freezes socket_tx_bp.*, refill leave-one-empty full under
// TCTL freeze, enable TX, then post_tx_wait must block on TX Event and succeed.
bool run_socket_tx_wait_phase(volatile LegacyTxDescriptor* tx_ring,
                              uint8_t* tx_buffer, uint64_t first_tick) {
    constexpr uint8_t wait_payload[] = {0x4D, 0x32, 0x38};
    constexpr size_t max_outstanding = descriptor_count - 1;

    info.socket_tx_wait.stage = 1;
    scheduler::event_init(tx_space_gate);
    cpu::disable_interrupts();
    __atomic_store_n(&socket_tx_bp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_wait_active, true, __ATOMIC_RELEASE);
    cpu::enable_interrupts();

    int tx_slot = -1;
    for (size_t index = 0; index < net::udp_socket_slots; ++index) {
        if (udp_rx_sockets.sockets[index].used &&
            udp_rx_sockets.sockets[index].local_port == 0x4D25) {
            tx_slot = static_cast<int>(index);
            break;
        }
    }
    if (tx_slot < 0) {
        info.socket_tx_wait.failure = 1;
        return false;
    }

    const net::UdpWireEndpoints endpoints{info.packet.mac, peer_mac, nic_ipv4,
                                          peer_ipv4};
    const uint16_t frame_length = net::udp_send(
        udp_rx_sockets, static_cast<size_t>(tx_slot), tx_buffer, 2048, endpoints,
        net::peer_udp_port, 0x4D28, wait_payload,
        static_cast<uint16_t>(sizeof(wait_payload)));
    if (frame_length < net::udp_min_frame_bytes) {
        info.socket_tx_wait.failure = 2;
        return false;
    }

    uint32_t tdh = mmio_read(reg_tdh0);
    uint32_t tdt = mmio_read(reg_tdt0);
    if (tdh != tdt || net::tx_ring_free(tdh, tdt) != max_outstanding) {
        info.socket_tx_wait.failure = 3;
        return false;
    }

    const uint32_t tctl_saved = mmio_read(reg_tctl);
    mmio_write(reg_tctl, tctl_saved & ~tctl_enable);

    info.socket_tx_wait.stage = 2;
    for (size_t step = 0; step < max_outstanding; ++step) {
        tdt = mmio_read(reg_tdt0);
        if (!try_post_tx_frame(tx_ring, tdt, frame_length)) {
            mmio_write(reg_tctl, tctl_saved);
            info.socket_tx_wait.failure = 4;
            return false;
        }
    }
    tdh = mmio_read(reg_tdh0);
    tdt = mmio_read(reg_tdt0);
    // Freeze-path reject proves the wait path starts from a full ring (TCTL
    // may free a slot before the first post_tx_wait attempt after unfreeze).
    if (!net::tx_ring_full(tdh, tdt) ||
        try_post_tx_frame(tx_ring, tdt, frame_length)) {
        mmio_write(reg_tctl, tctl_saved);
        info.socket_tx_wait.failure = 5;
        return false;
    }
    info.socket_tx_wait.waits = 1;

    info.socket_tx_wait.stage = 3;
    mmio_write(reg_tctl, tctl_saved);
    // M32: require at least one TX-space Event before posting. Unfreeze may
    // free a slot before post_tx_wait runs; waiting first keeps the contract
    // Event-shaped instead of a bare poll race.
    uint32_t events = 0;
    while (!scheduler::event_try_consume(tx_space_gate)) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            info.socket_tx_wait.failure = 6;
            return false;
        }
        cpu::wait_for_interrupt();
        const uint32_t tdh_now = mmio_read(reg_tdh0);
        const uint32_t tdt_now = mmio_read(reg_tdt0);
        if (!net::tx_ring_full(tdh_now, tdt_now)) {
            (void)scheduler::signal(tx_space_gate);
        }
    }
    ++events;
    uint32_t blocked = 0;
    uint32_t more_events = 0;
    const uint32_t tdt_post = mmio_read(reg_tdt0);
    if (!try_post_tx_frame(tx_ring, tdt_post, frame_length)) {
        if (!post_tx_wait(tx_ring, frame_length, first_tick, blocked,
                          more_events)) {
            info.socket_tx_wait.failure = 6;
            return false;
        }
        events += more_events;
    }
    (void)blocked;
    info.socket_tx_wait.posted = 1;
    info.socket_tx_wait.events = events;

    while (mmio_read(reg_tdh0) != mmio_read(reg_tdt0)) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            info.socket_tx_wait.failure = 7;
            return false;
        }
        cpu::wait_for_interrupt();
    }
    info.socket_tx_wait.tx_head = mmio_read(reg_tdh0);
    info.socket_tx_wait.tx_tail = mmio_read(reg_tdt0);
    if (info.socket_tx_wait.tx_head != info.socket_tx_wait.tx_tail ||
        network_tx_interrupts(NetworkPhase::UdpSocketTxWait) == 0 ||
        info.socket_tx_wait.events == 0 || network_vector_errors()) {
        info.socket_tx_wait.failure = 8;
        return false;
    }

    info.socket_tx_wait.stage = 4;
    return true;
}

bool run_udp_phase(volatile LegacyRxDescriptor* rx_ring, uint8_t** rx_buffers,
                   volatile LegacyTxDescriptor* tx_ring, uint8_t* tx_buffer,
                   uint64_t first_tick) {
    info.udp.stage = 1;
    info.udp.rx_head = mmio_read(reg_rdh0);
    info.udp.rx_tail = mmio_read(reg_rdt0);
    info.udp.tx_head = mmio_read(reg_tdh0);
    info.udp.tx_tail = mmio_read(reg_tdt0);
    if (info.udp.rx_head != 3 || info.udp.rx_tail != 7 ||
        info.udp.tx_head != 3 || info.udp.tx_tail != 3) {
        info.udp.failure = 7;
        return false;
    }

    net::udp_sockets_init(udp_rx_sockets);
    udp_rx_socket_slot = net::udp_bind(udp_rx_sockets, net::nic_udp_port);
    if (udp_rx_socket_slot < 0) {
        info.udp.failure = 9;
        return false;
    }
    info.udp.socket_delivered = 0;
    info.udp.socket_received = 0;
    info.irq_rx = {};
    info.irq_rx.stage = 1;
    irq_rx_next = info.udp.rx_head;
    __atomic_store_n(&irq_bsp_progress, 0, __ATOMIC_RELEASE);

    cpu::disable_interrupts();
    __atomic_store_n(&protocol_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&udp_active, true, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_irq_rx_active, true, __ATOMIC_RELEASE);
    log::line("[net] e1000e udp-ready");
    cpu::enable_interrupts();
    info.udp.stage = 2;

    for (size_t index = 0; index < udp_test_datagrams; ++index) {
        if (!run_udp_datagram(rx_ring, rx_buffers, tx_ring, tx_buffer,
                              first_tick, index, NetworkPhase::Udp)) {
            return false;
        }
    }
    // Refresh heads after the last recycle; IRQ Echo may have raced mid-flight.
    info.udp.rx_head = mmio_read(reg_rdh0);
    info.udp.tx_head = mmio_read(reg_tdh0);
    info.udp.tx_tail = mmio_read(reg_tdt0);
    if (info.udp.requests != udp_test_datagrams ||
        info.udp.replies != udp_test_datagrams ||
        info.udp.payload_bytes != udp_expected_payload_bytes ||
        info.udp.rx_bytes != udp_expected_frame_bytes ||
        info.udp.tx_bytes != udp_expected_frame_bytes ||
        info.udp.rx_head != 5 || info.udp.tx_head != 5 ||
        info.udp.rx_tail != 1 || info.udp.tx_tail != 5 ||
        info.udp.rx_head_wraps != 1 || info.udp.tx_head_wraps != 1 ||
        info.udp.rx_tail_wraps != 2 || info.udp.tx_tail_wraps != 1 ||
        info.udp.recycled_buffers != udp_test_datagrams ||
        info.udp.socket_delivered != udp_test_datagrams ||
        info.udp.socket_received != udp_test_datagrams ||
        __atomic_load_n(&info.irq_rx.delivered, __ATOMIC_ACQUIRE) <
            udp_test_datagrams ||
        __atomic_load_n(&info.irq_rx.echoes, __ATOMIC_ACQUIRE) <
            udp_test_datagrams ||
        __atomic_load_n(&info.irq_rx.recycled, __ATOMIC_ACQUIRE) <
            udp_test_datagrams ||
        network_vector_errors()) {
        info.udp.failure = 8;
        return false;
    }
    info.udp.stage = 3;

    // M13 serial evidence stays frozen in info.udp; M14 uses info.stress;
    // M25 socket TX freezes info.socket_tx; M27 uses info.socket_tx_bp;
    // M28 uses info.socket_tx_wait; M29–M31 irq_rx covers M13+M14.
    if (!run_udp_stress_phase(rx_ring, rx_buffers, tx_ring, tx_buffer,
                              first_tick)) {
        return false;
    }
    const uint32_t irq_expected =
        static_cast<uint32_t>(udp_test_datagrams + udp_stress_datagrams);
    if (__atomic_load_n(&info.irq_rx.delivered, __ATOMIC_ACQUIRE) !=
            irq_expected ||
        __atomic_load_n(&info.irq_rx.echoes, __ATOMIC_ACQUIRE) !=
            irq_expected ||
        __atomic_load_n(&info.irq_rx.recycled, __ATOMIC_ACQUIRE) !=
            irq_expected) {
        info.irq_rx.failure = 1;
        return false;
    }
    info.irq_rx.stage = 2;

    if (!run_socket_tx_phase(tx_ring, tx_buffer, first_tick)) {
        return false;
    }
    if (!run_socket_tx_bp_phase(tx_ring, tx_buffer, first_tick)) {
        return false;
    }
    return run_socket_tx_wait_phase(tx_ring, tx_buffer, first_tick);
}

}  // namespace

bool run_packet_phase() {
    volatile LegacyRxDescriptor* rx_ring = nullptr;
    volatile LegacyTxDescriptor* tx_ring = nullptr;
    uint8_t* rx_buffers[descriptor_count] = {};
    uint8_t* tx_buffer = nullptr;
    bool success = false;
    info.packet.stage = 1;
    if (!prepare_packet_rings(rx_ring, rx_buffers, tx_ring, tx_buffer)) {
        mmio_write(reg_rctl, 0);
        mmio_write(reg_tctl, 0);
        mmio_write(reg_rxdctl0, 0);
        mmio_write(reg_txdctl0, 0);
        return false;
    }

    info.packet.stage = 2;
    apic::start_periodic();
    const uint64_t first_tick = scheduler::ticks();
    __atomic_store_n(&packet_active, true, __ATOMIC_RELEASE);
    log::line("[net] e1000e packet-ready");
    cpu::enable_interrupts();

    do {
        if (!wait_for_packet_rx(rx_ring[0], first_tick, NetworkPhase::Raw, 1)) {
            break;
        }
        const uint8_t rx_status = rx_ring[0].status;
        const uint8_t rx_errors = rx_ring[0].errors;
        const uint16_t rx_length = rx_ring[0].length;
        info.packet.rx_head = mmio_read(reg_rdh0);
        info.packet.rx_length = rx_length;
        if ((rx_status & (descriptor_done | rx_descriptor_eop)) !=
                (descriptor_done | rx_descriptor_eop) ||
            rx_errors != 0 || info.packet.rx_head != 1 ||
            __atomic_load_n(&info.packet.rx_interrupts, __ATOMIC_ACQUIRE) != 1 ||
            !valid_request_frame(rx_buffers[0], rx_length)) {
            break;
        }
        if (!rx_guard_intact(rx_buffers[0], rx_length)) {
            break;
        }
        info.packet.rx_frames = 1;

        // Build the exact response that run_qemu.py independently validates.
        constexpr uint8_t magic[8] = {'C', 'X', 'C', '1', '1', 'R', 'S', 'P'};
        for (size_t index = 0; index < 6; ++index) {
            tx_buffer[index] = rx_buffers[0][6 + index];
            tx_buffer[6 + index] = info.packet.mac[index];
        }
        tx_buffer[12] = 0x88;
        tx_buffer[13] = 0xb5;
        for (size_t index = 0; index < 8; ++index) {
            tx_buffer[14 + index] = magic[index];
        }
        for (size_t index = 8; index < 46; ++index) {
            tx_buffer[14 + index] = rx_buffers[0][14 + index] ^ 0xa5;
        }

        info.packet.stage = 3;
        if (!submit_tx_frame(tx_ring, 0, packet_bytes, first_tick, NetworkPhase::Raw, 1)) {
            break;
        }
        info.packet.tx_head = mmio_read(reg_tdh0);
        info.packet.tx_length = packet_bytes;
        if (info.packet.tx_head != 1 ||
            __atomic_load_n(&info.packet.tx_interrupts, __ATOMIC_ACQUIRE) != 1) {
            break;
        }
        if (network_vector_errors()) {
            break;
        }
        info.packet.tx_frames = 1;
        success = true;
    } while (false);

    if (success) {
        info.packet.stage = 4;
        success = run_protocol_phase(rx_ring, rx_buffers, tx_ring, tx_buffer,
                                     first_tick);
    }
    if (success) {
        success = run_udp_phase(rx_ring, rx_buffers, tx_ring, tx_buffer,
                                first_tick);
    }

    cpu::disable_interrupts();
    __atomic_store_n(&socket_irq_rx_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_wait_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_bp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_tx_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&udp_stress_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&udp_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&protocol_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&packet_active, false, __ATOMIC_RELEASE);
    apic::stop_periodic();
    mmio_write(reg_rctl, 0);
    mmio_write(reg_tctl, 0);
    mmio_write(reg_rxdctl0, 0);
    mmio_write(reg_txdctl0, 0);
    return success;
}

}  // namespace cxc::e1000e::detail
