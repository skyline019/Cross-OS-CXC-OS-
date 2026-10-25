#include <cxc/net/datagram.hpp>

#include <cxc/net/checksum.hpp>
#include <cxc/net/proto.hpp>

extern "C" void* memset(void*, int, size_t);

namespace cxc::net {

bool parse_udp_datagram(const uint8_t* frame, uint16_t length,
                        UdpDatagramView& out) {
    out = UdpDatagramView{};
    if (frame == nullptr || length < 42 || length > 2048) {
        return false;
    }
    const uint16_t ip_total = read_be16(frame + 16);
    const size_t ip_frame_length = static_cast<size_t>(ip_total) + 14;
    const size_t expected_frame_length =
        ip_frame_length < udp_min_frame_bytes ? udp_min_frame_bytes
                                              : ip_frame_length;
    const uint16_t udp_length = read_be16(frame + 38);
    if (ip_total < 28 || expected_frame_length != length ||
        read_be16(frame + 12) != ethertype_ipv4 || frame[14] != 0x45 ||
        frame[15] != 0 || (read_be16(frame + 20) & 0x3fffu) != 0 ||
        frame[22] == 0 || frame[23] != ip_protocol_udp ||
        internet_checksum(frame + 14, ipv4_header_bytes) != 0 ||
        udp_length < udp_header_bytes || udp_length != ip_total - 20 ||
        read_be16(frame + 40) == 0 ||
        udp_checksum(frame + 14, frame + 34) != 0) {
        return false;
    }
    for (size_t index = ip_frame_length; index < length; ++index) {
        if (frame[index] != 0) {
            return false;
        }
    }
    out.src_port = read_be16(frame + 34);
    out.dst_port = read_be16(frame + 36);
    out.identification = read_be16(frame + 18);
    out.payload = frame + 42;
    out.payload_length = static_cast<uint16_t>(udp_length - udp_header_bytes);
    out.frame_length = length;
    return true;
}

uint16_t build_udp_datagram(uint8_t* buffer, uint16_t capacity,
                            const UdpWireEndpoints& endpoints,
                            uint16_t src_port, uint16_t dst_port,
                            uint16_t identification, const uint8_t* payload,
                            uint16_t payload_length) {
    if (buffer == nullptr || endpoints.local_mac == nullptr ||
        endpoints.remote_mac == nullptr || endpoints.local_ipv4 == nullptr ||
        endpoints.remote_ipv4 == nullptr ||
        (payload_length > 0 && payload == nullptr) ||
        payload_length > udp_max_payload_bytes) {
        return 0;
    }
    const uint16_t udp_length =
        static_cast<uint16_t>(udp_header_bytes + payload_length);
    const uint16_t ip_total =
        static_cast<uint16_t>(ipv4_header_bytes + udp_length);
    const size_t ip_frame_length =
        static_cast<size_t>(ethernet_header_bytes) + ip_total;
    const uint16_t frame_length = static_cast<uint16_t>(
        ip_frame_length < udp_min_frame_bytes ? udp_min_frame_bytes
                                              : ip_frame_length);
    if (capacity < frame_length) {
        return 0;
    }

    memset(buffer, 0, frame_length);
    bytes_copy(buffer, endpoints.remote_mac, 6);
    bytes_copy(buffer + 6, endpoints.local_mac, 6);
    write_be16(buffer + 12, ethertype_ipv4);
    buffer[14] = 0x45;
    buffer[15] = 0;
    write_be16(buffer + 16, ip_total);
    write_be16(buffer + 18, identification);
    write_be16(buffer + 20, 0);
    buffer[22] = 64;
    buffer[23] = ip_protocol_udp;
    write_be16(buffer + 24, 0);
    bytes_copy(buffer + 26, endpoints.local_ipv4, 4);
    bytes_copy(buffer + 30, endpoints.remote_ipv4, 4);
    write_be16(buffer + 24, internet_checksum(buffer + 14, ipv4_header_bytes));
    write_be16(buffer + 34, src_port);
    write_be16(buffer + 36, dst_port);
    write_be16(buffer + 38, udp_length);
    write_be16(buffer + 40, 0);
    if (payload_length != 0) {
        bytes_copy(buffer + 42, payload, payload_length);
    }
    uint16_t checksum = udp_checksum(buffer + 14, buffer + 34);
    if (checksum == 0) {
        checksum = 0xffff;
    }
    write_be16(buffer + 40, checksum);
    return frame_length;
}

}  // namespace cxc::net
