#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::net {

uint16_t read_be16(const uint8_t* bytes);
void write_be16(uint8_t* bytes, uint16_t value);

// RFC 1071 internet checksum. Returns the one's-complement value to store;
// verifying a field already containing the checksum yields 0 when valid.
uint16_t internet_checksum(const uint8_t* bytes, size_t length);

// UDP checksum over the IPv4 pseudo-header (src/dst/proto/length) plus the
// UDP header and payload. ipv4 points at the 20-byte header; udp at the UDP
// header whose length field is authoritative.
uint16_t udp_checksum(const uint8_t* ipv4, const uint8_t* udp);

}  // namespace cxc::net
