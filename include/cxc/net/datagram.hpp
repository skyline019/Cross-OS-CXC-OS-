#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::net {

// Minimal host-facing UDP datagram helpers (M20). Pure frame builders/parsers;
// no socket table, no RX queue, no driver TX. Echo self-test remains the
// bidirectional QEMU evidence path.

constexpr uint16_t udp_header_bytes = 8;
constexpr uint16_t ipv4_header_bytes = 20;
constexpr uint16_t ethernet_header_bytes = 14;
constexpr uint16_t udp_min_frame_bytes = 60;
constexpr uint16_t udp_max_payload_bytes = 1472;

struct UdpDatagramView {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t identification;
    const uint8_t* payload;
    uint16_t payload_length;
    uint16_t frame_length;
};

struct UdpWireEndpoints {
    const uint8_t* local_mac;     // 6
    const uint8_t* remote_mac;    // 6
    const uint8_t* local_ipv4;    // 4
    const uint8_t* remote_ipv4;   // 4
};

// Structural parse: Ethernet/IPv4/UDP layout + checksums. Does not require the
// fixed echo port pair used by valid_udp_request.
bool parse_udp_datagram(const uint8_t* frame, uint16_t length,
                        UdpDatagramView& out);

// Build a padded Ethernet frame carrying one UDP datagram. Returns 0 on error.
uint16_t build_udp_datagram(uint8_t* buffer, uint16_t capacity,
                            const UdpWireEndpoints& endpoints,
                            uint16_t src_port, uint16_t dst_port,
                            uint16_t identification, const uint8_t* payload,
                            uint16_t payload_length);

}  // namespace cxc::net
