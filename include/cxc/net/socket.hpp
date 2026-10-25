#pragma once

#include <cxc/net/datagram.hpp>
#include <cxc/scheduler.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::net {

// Minimal UDP socket table (M21–M25). Fixed slots + per-socket RX queue of
// owned payload copies. M22: sticky Event + udp_recv_wait. M23: udp_send
// builds an Ethernet/UDP frame into a caller buffer. M25 selftest submits that
// frame via e1000e TX (net layer still has no driver dependency).
// No ephemeral ports, no sockaddr / AF abstraction.

constexpr size_t udp_socket_slots = 4;
constexpr size_t udp_socket_rx_depth = 4;
constexpr uint16_t udp_socket_payload_cap = 64;

struct UdpQueuedDatagram {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t identification;
    uint16_t payload_length;
    uint8_t payload[udp_socket_payload_cap];
};

struct UdpSocket {
    bool used;
    uint16_t local_port;
    uint16_t rx_count;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint32_t dropped;
    scheduler::Event rx_gate;
    UdpQueuedDatagram rx[udp_socket_rx_depth];
};

struct UdpSocketTable {
    UdpSocket sockets[udp_socket_slots];
};

void udp_sockets_init(UdpSocketTable& table);

// Bind local_port (must be non-zero, unique). Returns slot index or -1.
int udp_bind(UdpSocketTable& table, uint16_t local_port);

bool udp_close(UdpSocketTable& table, size_t slot);

// Parse Ethernet/IPv4/UDP frame and enqueue a payload copy on the socket
// bound to the destination port. On success, signals rx_gate (sticky).
bool udp_deliver(UdpSocketTable& table, const uint8_t* frame, uint16_t length);

bool udp_recv(UdpSocketTable& table, size_t slot, UdpQueuedDatagram& out);

// Worker-only: block on rx_gate until a datagram is available (or wait fails).
bool udp_recv_wait(UdpSocketTable& table, size_t slot, UdpQueuedDatagram& out);

uint16_t udp_rx_pending(const UdpSocketTable& table, size_t slot);

// Build a padded Ethernet/UDP frame from a bound slot. src_port is the
// socket's local_port. Returns frame length, or 0 on error. Transmission is
// the caller's responsibility (M25 selftest copies into the TX DMA page).
uint16_t udp_send(UdpSocketTable& table, size_t slot, uint8_t* buffer,
                  uint16_t capacity, const UdpWireEndpoints& endpoints,
                  uint16_t dst_port, uint16_t identification,
                  const uint8_t* payload, uint16_t payload_length);

// Freestanding smoke: bind/deliver/recv plus send→parse round-trip.
bool udp_socket_contract_ok();

}  // namespace cxc::net
