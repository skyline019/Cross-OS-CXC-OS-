#include <cxc/net/checksum.hpp>

namespace cxc::net {

uint16_t read_be16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                                 bytes[1]);
}

void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8);
    bytes[1] = static_cast<uint8_t>(value);
}

uint16_t internet_checksum(const uint8_t* bytes, size_t length) {
    uint32_t sum = 0;
    while (length >= 2) {
        sum += read_be16(bytes);
        bytes += 2;
        length -= 2;
    }
    if (length != 0) {
        sum += static_cast<uint16_t>(bytes[0]) << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

uint16_t udp_checksum(const uint8_t* ipv4, const uint8_t* udp) {
    constexpr uint8_t ip_protocol_udp = 17;
    uint32_t sum = 0;
    for (size_t offset = 12; offset < 20; offset += 2) {
        sum += read_be16(ipv4 + offset);
    }
    sum += ip_protocol_udp;
    const uint16_t udp_length = read_be16(udp + 4);
    sum += udp_length;
    size_t remaining = udp_length;
    while (remaining >= 2) {
        sum += read_be16(udp);
        udp += 2;
        remaining -= 2;
    }
    if (remaining != 0) {
        sum += static_cast<uint16_t>(udp[0]) << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

}  // namespace cxc::net
