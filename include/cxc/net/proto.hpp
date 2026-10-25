#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::net {

constexpr uint16_t ethertype_arp = 0x0806;
constexpr uint16_t ethertype_ipv4 = 0x0800;
constexpr uint8_t ip_protocol_udp = 17;
constexpr uint16_t nic_udp_port = 7;
constexpr uint16_t peer_udp_port = 0xc123;

struct Endpoint {
    const uint8_t* nic_mac;    // 6 bytes
    const uint8_t* peer_mac;   // 6 bytes
    const uint8_t* nic_ipv4;   // 4 bytes
    const uint8_t* peer_ipv4;  // 4 bytes
};

bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t count);
void bytes_copy(uint8_t* destination, const uint8_t* source, size_t count);

bool valid_arp_request(const uint8_t* frame, uint16_t length,
                       const Endpoint& endpoint);
uint16_t build_arp_reply(uint8_t* tx_buffer, const Endpoint& endpoint);

bool valid_icmp_echo_request(const uint8_t* frame, uint16_t length,
                             const Endpoint& endpoint);
uint16_t build_icmp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                               uint16_t length, const Endpoint& endpoint);

bool valid_udp_request(const uint8_t* frame, uint16_t length,
                       const Endpoint& endpoint);
uint16_t build_udp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                              uint16_t request_length,
                              const Endpoint& endpoint);

}  // namespace cxc::net
