#include <cxc/net/proto.hpp>

#include <cxc/net/checksum.hpp>
#include <cxc/net/datagram.hpp>

extern "C" void* memset(void*, int, size_t);

namespace cxc::net {
namespace {

constexpr uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t zero_mac[6] = {};

}  // namespace

bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

void bytes_copy(uint8_t* destination, const uint8_t* source, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        destination[index] = source[index];
    }
}

bool valid_arp_request(const uint8_t* frame, uint16_t length,
                       const Endpoint& endpoint) {
    if (frame == nullptr || length != 60 ||
        !bytes_equal(frame, broadcast_mac, 6) ||
        !bytes_equal(frame + 6, endpoint.peer_mac, 6) ||
        read_be16(frame + 12) != ethertype_arp ||
        read_be16(frame + 14) != 1 || read_be16(frame + 16) != ethertype_ipv4 ||
        frame[18] != 6 || frame[19] != 4 || read_be16(frame + 20) != 1 ||
        !bytes_equal(frame + 22, endpoint.peer_mac, 6) ||
        !bytes_equal(frame + 28, endpoint.peer_ipv4, 4) ||
        !bytes_equal(frame + 32, zero_mac, 6) ||
        !bytes_equal(frame + 38, endpoint.nic_ipv4, 4)) {
        return false;
    }
    for (size_t index = 42; index < 60; ++index) {
        if (frame[index] != 0) {
            return false;
        }
    }
    return true;
}

uint16_t build_arp_reply(uint8_t* tx_buffer, const Endpoint& endpoint) {
    memset(tx_buffer, 0, 60);
    bytes_copy(tx_buffer, endpoint.peer_mac, 6);
    bytes_copy(tx_buffer + 6, endpoint.nic_mac, 6);
    write_be16(tx_buffer + 12, ethertype_arp);
    write_be16(tx_buffer + 14, 1);
    write_be16(tx_buffer + 16, ethertype_ipv4);
    tx_buffer[18] = 6;
    tx_buffer[19] = 4;
    write_be16(tx_buffer + 20, 2);
    bytes_copy(tx_buffer + 22, endpoint.nic_mac, 6);
    bytes_copy(tx_buffer + 28, endpoint.nic_ipv4, 4);
    bytes_copy(tx_buffer + 32, endpoint.peer_mac, 6);
    bytes_copy(tx_buffer + 38, endpoint.peer_ipv4, 4);
    return 60;
}

bool valid_icmp_echo_request(const uint8_t* frame, uint16_t length,
                             const Endpoint& endpoint) {
    if (frame == nullptr || length < 42 || length > 2048 ||
        !bytes_equal(frame, endpoint.nic_mac, 6) ||
        !bytes_equal(frame + 6, endpoint.peer_mac, 6) ||
        read_be16(frame + 12) != ethertype_ipv4 || frame[14] != 0x45 ||
        frame[15] != 0 || read_be16(frame + 16) != length - 14 ||
        (read_be16(frame + 20) & 0x3fffu) != 0 || frame[22] == 0 ||
        frame[23] != 1 || !bytes_equal(frame + 26, endpoint.peer_ipv4, 4) ||
        !bytes_equal(frame + 30, endpoint.nic_ipv4, 4) ||
        internet_checksum(frame + 14, 20) != 0 || frame[34] != 8 ||
        frame[35] != 0 ||
        internet_checksum(frame + 34, static_cast<size_t>(length) - 34) != 0) {
        return false;
    }
    return true;
}

uint16_t build_icmp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                               uint16_t length, const Endpoint& endpoint) {
    bytes_copy(tx_buffer, request, length);
    bytes_copy(tx_buffer, endpoint.peer_mac, 6);
    bytes_copy(tx_buffer + 6, endpoint.nic_mac, 6);
    bytes_copy(tx_buffer + 26, endpoint.nic_ipv4, 4);
    bytes_copy(tx_buffer + 30, request + 26, 4);
    tx_buffer[22] = 64;
    write_be16(tx_buffer + 24, 0);
    write_be16(tx_buffer + 24, internet_checksum(tx_buffer + 14, 20));
    tx_buffer[34] = 0;
    tx_buffer[35] = 0;
    write_be16(tx_buffer + 36, 0);
    write_be16(tx_buffer + 36,
               internet_checksum(tx_buffer + 34,
                                 static_cast<size_t>(length) - 34));
    return length;
}

bool valid_udp_request(const uint8_t* frame, uint16_t length,
                       const Endpoint& endpoint) {
    if (frame == nullptr || length < 42 || length > 2048) {
        return false;
    }
    const uint16_t ip_total = read_be16(frame + 16);
    const size_t ip_frame_length = static_cast<size_t>(ip_total) + 14;
    const size_t expected_frame_length =
        ip_frame_length < 60 ? 60 : ip_frame_length;
    const uint16_t udp_length = read_be16(frame + 38);
    if (ip_total < 28 || expected_frame_length != length ||
        !bytes_equal(frame, endpoint.nic_mac, 6) ||
        !bytes_equal(frame + 6, endpoint.peer_mac, 6) ||
        read_be16(frame + 12) != ethertype_ipv4 || frame[14] != 0x45 ||
        frame[15] != 0 || (read_be16(frame + 20) & 0x3fffu) != 0 ||
        frame[22] == 0 || frame[23] != ip_protocol_udp ||
        !bytes_equal(frame + 26, endpoint.peer_ipv4, 4) ||
        !bytes_equal(frame + 30, endpoint.nic_ipv4, 4) ||
        internet_checksum(frame + 14, 20) != 0 ||
        read_be16(frame + 34) != peer_udp_port ||
        read_be16(frame + 36) != nic_udp_port || udp_length < 8 ||
        udp_length != ip_total - 20 || read_be16(frame + 40) == 0 ||
        udp_checksum(frame + 14, frame + 34) != 0) {
        return false;
    }
    for (size_t index = ip_frame_length; index < length; ++index) {
        if (frame[index] != 0) {
            return false;
        }
    }
    return true;
}

uint16_t build_udp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                              uint16_t request_length,
                              const Endpoint& endpoint) {
    UdpDatagramView view{};
    if (!parse_udp_datagram(request, request_length, view) ||
        view.dst_port != nic_udp_port) {
        return 0;
    }
    const UdpWireEndpoints wire{endpoint.nic_mac, endpoint.peer_mac,
                                endpoint.nic_ipv4, endpoint.peer_ipv4};
    // Echo: swap ports, keep identification and payload.
    return build_udp_datagram(tx_buffer, 2048, wire, nic_udp_port,
                              view.src_port, view.identification, view.payload,
                              view.payload_length);
}

}  // namespace cxc::net
