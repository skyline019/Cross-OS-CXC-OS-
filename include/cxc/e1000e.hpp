#pragma once

#include <stdint.h>

namespace cxc::e1000e {

struct VectorResult {
    uint64_t calls;
    uint64_t matched_calls;
    uint64_t empty_calls;
    uint64_t cross_calls;
    uint32_t last_cause;
    uint32_t errors;
    uint32_t table_entry;
    uint32_t idt_vector;
    uint32_t expected_cause;
    uint32_t error_cause;
    uint32_t reserved;
};

struct PacketResult {
    uint64_t rx_interrupts;
    uint64_t tx_interrupts;
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint32_t rx_length;
    uint32_t tx_length;
    uint32_t rx_head;
    uint32_t tx_head;
    uint32_t stage;
    uint32_t reserved;
    uint8_t mac[6];
    uint8_t padding[2];
};

struct ProtocolResult {
    uint64_t rx_interrupts;
    uint64_t tx_interrupts;
    uint64_t arp_requests;
    uint64_t arp_replies;
    uint64_t icmp_requests;
    uint64_t icmp_replies;
    uint32_t arp_rx_bytes;
    uint32_t arp_tx_bytes;
    uint32_t icmp_rx_bytes;
    uint32_t icmp_tx_bytes;
    uint32_t rx_head;
    uint32_t tx_head;
    uint32_t stage;
    uint32_t reserved;
};

struct UdpResult {
    uint64_t rx_interrupts;
    uint64_t tx_interrupts;
    uint64_t requests;
    uint64_t replies;
    uint64_t payload_bytes;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t rx_head;
    uint32_t tx_head;
    uint32_t rx_tail;
    uint32_t tx_tail;
    uint32_t rx_head_wraps;
    uint32_t tx_head_wraps;
    uint32_t rx_tail_wraps;
    uint32_t tx_tail_wraps;
    uint32_t recycled_buffers;
    uint32_t last_rx_bytes;
    uint32_t last_tx_bytes;
    uint32_t datagram;
    uint32_t rx_descriptor;
    uint32_t tx_descriptor;
    uint32_t stage;
    uint32_t failure;
    // M24: RX frames demuxed into a bound UDP socket (deliver+recv per datagram).
    uint32_t socket_delivered;
    uint32_t socket_received;
};

// M14: post-M13 long-stability / wrap pressure. Does not replace the M13
// ten-datagram contract; counters stay separate so serial evidence stays 10/10.
// M26: stress also demuxes into the M13-bound socket (independent of udp.*).
struct UdpStressResult {
    uint64_t rx_interrupts;
    uint64_t tx_interrupts;
    uint64_t requests;
    uint64_t replies;
    uint64_t payload_bytes;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t rx_head;
    uint32_t tx_head;
    uint32_t rx_tail;
    uint32_t tx_tail;
    uint32_t rx_head_wraps;
    uint32_t tx_head_wraps;
    uint32_t rx_tail_wraps;
    uint32_t tx_tail_wraps;
    uint32_t recycled_buffers;
    uint32_t datagram;
    uint32_t stage;
    uint32_t failure;
    uint32_t socket_delivered;
    uint32_t socket_received;
};

// M25: post-stress socket→e1000e TX submit. Independent of M13 Echo / M14
// stress counters; does not require peer validation of the extra frame.
struct UdpSocketTxResult {
    uint64_t tx_interrupts;
    uint64_t frames;
    uint64_t bytes;
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t stage;
    uint32_t failure;
};

// M27: after M25 freeze, fill TX ring to leave-one-empty full, prove reject,
// then drain. Independent of socket_tx.*; peer does not validate these frames.
struct UdpSocketTxBpResult {
    uint64_t tx_interrupts;
    uint32_t posted;
    uint32_t rejects;
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t stage;
    uint32_t failure;
};

// M28/M32: after M27 freeze, refill leave-one-empty full, then post_tx_wait
// blocks on TX Event until TDH frees a slot. Peer ignores frames.
struct UdpSocketTxWaitResult {
    uint64_t tx_interrupts;
    uint32_t waits;
    uint32_t posted;
    uint32_t events;
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t stage;
    uint32_t failure;
};

// M29–M31: M13+M14 UDP deliver + Echo TX + RDT recycle from RXQ0/TXQ0;
// BSP drains recv, waits TX DD / recycled. delivered/echoes/recycled = 266.
// M24/M26 deliver/recv serial stays frozen.
struct UdpIrqRxResult {
    uint32_t delivered;
    uint32_t echoes;
    uint32_t recycled;
    uint32_t stage;
    uint32_t failure;
};

struct TestInfo {
    uint64_t csr_physical;
    uint64_t msix_physical;
    uint32_t table_entries;
    uint32_t per_vector_pba_seen;
    uint32_t function_pba_seen;
    uint32_t stage;
    VectorResult vectors[3];
    PacketResult packet;
    ProtocolResult protocol;
    UdpResult udp;
    UdpStressResult stress;
    UdpSocketTxResult socket_tx;
    UdpSocketTxBpResult socket_tx_bp;
    UdpSocketTxWaitResult socket_tx_wait;
    UdpIrqRxResult irq_rx;
};

// Runs ICS/IVAR/PBA, M11–M14/M26/M29–M31, M25–M28 socket TX path.
// run_qemu.py checks M11–M14 responses. DMA/IRQ state and packet pages cleaned.
bool run_msix_test();
const TestInfo& test_info();

}  // namespace cxc::e1000e
