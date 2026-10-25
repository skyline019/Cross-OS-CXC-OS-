// Host-side unit tests for freestanding cxc::net protocol helpers.
#include <cxc/net/checksum.hpp>
#include <cxc/net/datagram.hpp>
#include <cxc/net/proto.hpp>
#include <cxc/net/ring_model.hpp>
#include <cxc/net/socket.hpp>

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

constexpr uint8_t nic_mac[6] = {0x52, 0x54, 0x00, 0x43, 0x58, 0x43};
constexpr uint8_t peer_mac[6] = {0x02, 0x43, 0x58, 0x43, 0x11, 0x01};
constexpr uint8_t nic_ipv4[4] = {10, 0, 12, 2};
constexpr uint8_t peer_ipv4[4] = {10, 0, 12, 1};

cxc::net::Endpoint endpoint() {
    return cxc::net::Endpoint{nic_mac, peer_mac, nic_ipv4, peer_ipv4};
}

void put16(uint8_t* bytes, uint16_t value) {
    cxc::net::write_be16(bytes, value);
}

void test_checksum_roundtrip() {
    uint8_t data[8] = {0x45, 0x00, 0x00, 0x3c, 0x12, 0x34, 0x00, 0x00};
    const uint16_t sum = cxc::net::internet_checksum(data, 8);
    put16(data + 6, sum);
    expect(cxc::net::internet_checksum(data, 8) == 0, "ipv4-style checksum verifies");
}

void test_odd_length_checksum() {
    uint8_t data[3] = {0x12, 0x34, 0x56};
    const uint16_t sum = cxc::net::internet_checksum(data, 3);
    expect(sum != 0, "odd-length checksum nonzero");
    uint8_t padded[4] = {0x12, 0x34, 0x56, 0x00};
    expect(cxc::net::internet_checksum(data, 3) ==
               cxc::net::internet_checksum(padded, 4),
           "odd length pads high byte");
}

void build_arp_request(uint8_t* frame) {
    std::memset(frame, 0, 60);
    std::memset(frame, 0xff, 6);
    std::memcpy(frame + 6, peer_mac, 6);
    put16(frame + 12, cxc::net::ethertype_arp);
    put16(frame + 14, 1);
    put16(frame + 16, cxc::net::ethertype_ipv4);
    frame[18] = 6;
    frame[19] = 4;
    put16(frame + 20, 1);
    std::memcpy(frame + 22, peer_mac, 6);
    std::memcpy(frame + 28, peer_ipv4, 4);
    std::memcpy(frame + 38, nic_ipv4, 4);
}

void test_arp() {
    uint8_t request[60];
    build_arp_request(request);
    expect(cxc::net::valid_arp_request(request, 60, endpoint()),
           "valid ARP request accepted");
    request[42] = 1;
    expect(!cxc::net::valid_arp_request(request, 60, endpoint()),
           "ARP padding must be zero");

    uint8_t reply[60];
    const uint16_t length = cxc::net::build_arp_reply(reply, endpoint());
    expect(length == 60, "ARP reply length 60");
    expect(std::memcmp(reply, peer_mac, 6) == 0, "ARP reply dst peer");
    expect(std::memcmp(reply + 6, nic_mac, 6) == 0, "ARP reply src nic");
    expect(cxc::net::read_be16(reply + 20) == 2, "ARP reply opcode");
}

void build_icmp_request(uint8_t* frame) {
    std::memset(frame, 0, 74);
    std::memcpy(frame, nic_mac, 6);
    std::memcpy(frame + 6, peer_mac, 6);
    put16(frame + 12, cxc::net::ethertype_ipv4);
    frame[14] = 0x45;
    put16(frame + 16, 60);
    frame[22] = 64;
    frame[23] = 1;
    std::memcpy(frame + 26, peer_ipv4, 4);
    std::memcpy(frame + 30, nic_ipv4, 4);
    put16(frame + 24, cxc::net::internet_checksum(frame + 14, 20));
    frame[34] = 8;
    frame[35] = 0;
    put16(frame + 36, cxc::net::internet_checksum(frame + 34, 40));
}

void test_icmp() {
    uint8_t request[74];
    build_icmp_request(request);
    expect(cxc::net::valid_icmp_echo_request(request, 74, endpoint()),
           "valid ICMP echo accepted");
    request[36] ^= 0x01;
    expect(!cxc::net::valid_icmp_echo_request(request, 74, endpoint()),
           "bad ICMP checksum rejected");
    request[36] ^= 0x01;

    uint8_t reply[74];
    const uint16_t length =
        cxc::net::build_icmp_echo_reply(reply, request, 74, endpoint());
    expect(length == 74, "ICMP reply length");
    expect(reply[34] == 0, "ICMP reply type echo-reply");
    expect(cxc::net::internet_checksum(reply + 14, 20) == 0,
           "ICMP reply IPv4 checksum");
    expect(cxc::net::internet_checksum(reply + 34, 40) == 0,
           "ICMP reply message checksum");
}

void build_udp_request(uint8_t* frame, size_t payload_length) {
    const size_t ip_total = 20 + 8 + payload_length;
    const size_t frame_length = ip_total + 14 < 60 ? 60 : ip_total + 14;
    std::memset(frame, 0, frame_length);
    std::memcpy(frame, nic_mac, 6);
    std::memcpy(frame + 6, peer_mac, 6);
    put16(frame + 12, cxc::net::ethertype_ipv4);
    frame[14] = 0x45;
    put16(frame + 16, static_cast<uint16_t>(ip_total));
    frame[22] = 53;
    frame[23] = cxc::net::ip_protocol_udp;
    std::memcpy(frame + 26, peer_ipv4, 4);
    std::memcpy(frame + 30, nic_ipv4, 4);
    put16(frame + 24, cxc::net::internet_checksum(frame + 14, 20));
    put16(frame + 34, cxc::net::peer_udp_port);
    put16(frame + 36, cxc::net::nic_udp_port);
    put16(frame + 38, static_cast<uint16_t>(8 + payload_length));
    for (size_t index = 0; index < payload_length; ++index) {
        frame[42 + index] = static_cast<uint8_t>(index + 1);
    }
    uint16_t checksum = cxc::net::udp_checksum(frame + 14, frame + 34);
    if (checksum == 0) {
        checksum = 0xffff;
    }
    put16(frame + 40, checksum);
}

void test_udp() {
    constexpr size_t payload = 3;
    constexpr size_t frame_length = 60;
    uint8_t request[frame_length];
    build_udp_request(request, payload);
    expect(cxc::net::valid_udp_request(request, frame_length, endpoint()),
           "valid UDP request accepted");
    request[40] ^= 0x01;
    expect(!cxc::net::valid_udp_request(request, frame_length, endpoint()),
           "bad UDP checksum rejected");
    request[40] ^= 0x01;

    uint8_t reply[frame_length];
    const uint16_t length = cxc::net::build_udp_echo_reply(
        reply, request, frame_length, endpoint());
    expect(length == frame_length, "UDP reply padded length");
    expect(cxc::net::read_be16(reply + 34) == cxc::net::nic_udp_port,
           "UDP reply src port");
    expect(cxc::net::udp_checksum(reply + 14, reply + 34) == 0,
           "UDP reply checksum verifies");
}

void test_udp_datagram_api() {
    constexpr uint16_t payload_length = 5;
    constexpr uint16_t identification = 0x5507;
    constexpr uint16_t src_port = 0xBEEF;
    constexpr uint16_t dst_port = 0xCAFE;
    uint8_t payload[payload_length] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint8_t frame[128];
    const cxc::net::UdpWireEndpoints wire{nic_mac, peer_mac, nic_ipv4,
                                          peer_ipv4};
    const uint16_t built = cxc::net::build_udp_datagram(
        frame, sizeof(frame), wire, src_port, dst_port, identification,
        payload, payload_length);
    expect(built == 60, "M20 datagram pads short frame to 60");

    cxc::net::UdpDatagramView view{};
    expect(cxc::net::parse_udp_datagram(frame, built, view),
           "M20 parse accepts built datagram");
    expect(view.src_port == src_port && view.dst_port == dst_port,
           "M20 ports round-trip");
    expect(view.identification == identification, "M20 id round-trip");
    expect(view.payload_length == payload_length, "M20 payload length");
    expect(std::memcmp(view.payload, payload, payload_length) == 0,
           "M20 payload bytes");

    // Echo path still goes through the datagram helpers.
    uint8_t echo_request[60];
    build_udp_request(echo_request, 3);
    uint8_t echo_reply[60];
    expect(cxc::net::build_udp_echo_reply(echo_reply, echo_request, 60,
                                          endpoint()) == 60,
           "echo reply via datagram API");
    cxc::net::UdpDatagramView echo_view{};
    expect(cxc::net::parse_udp_datagram(echo_reply, 60, echo_view),
           "parse echo reply");
    expect(echo_view.src_port == cxc::net::nic_udp_port, "echo src is NIC");
    expect(echo_view.dst_port == cxc::net::peer_udp_port, "echo dst is peer");
}

void test_udp_socket_api() {
    expect(cxc::net::udp_socket_contract_ok(), "M21 kernel contract helper");

    cxc::net::UdpSocketTable table{};
    cxc::net::udp_sockets_init(table);
    expect(cxc::net::udp_bind(table, 0) < 0, "port 0 rejected");
    const int slot_a = cxc::net::udp_bind(table, 0x1001);
    const int slot_b = cxc::net::udp_bind(table, 0x1002);
    expect(slot_a >= 0 && slot_b >= 0 && slot_a != slot_b, "two binds");
    expect(cxc::net::udp_bind(table, 0x1001) < 0, "duplicate port rejected");

    uint8_t payload[2] = {0xAA, 0xBB};
    uint8_t frame[128];
    // Incoming to 0x1001: peer → nic roles swapped in wire endpoints.
    const cxc::net::UdpWireEndpoints from_peer{peer_mac, nic_mac, peer_ipv4,
                                               nic_ipv4};
    const uint16_t built = cxc::net::build_udp_datagram(
        frame, sizeof(frame), from_peer, cxc::net::peer_udp_port, 0x1001,
        0x2211, payload, 2);
    expect(built == 60, "socket deliver frame length");
    expect(cxc::net::udp_deliver(table, frame, built), "deliver to 0x1001");
    expect(cxc::net::udp_rx_pending(table, static_cast<size_t>(slot_a)) == 1,
           "pending on A");
    expect(cxc::net::udp_rx_pending(table, static_cast<size_t>(slot_b)) == 0,
           "B empty");

    // Unbound destination is ignored.
    const uint16_t miss = cxc::net::build_udp_datagram(
        frame, sizeof(frame), from_peer, cxc::net::peer_udp_port, 0x1003,
        0x2212, payload, 2);
    expect(!cxc::net::udp_deliver(table, frame, miss), "unbound drop");

    // Fill RX queue then drop.
    for (uint16_t index = 0; index < cxc::net::udp_socket_rx_depth - 1;
         ++index) {
        const uint16_t more = cxc::net::build_udp_datagram(
            frame, sizeof(frame), from_peer, cxc::net::peer_udp_port, 0x1001,
            static_cast<uint16_t>(0x3000 + index), payload, 2);
        expect(cxc::net::udp_deliver(table, frame, more), "fill queue");
    }
    expect(cxc::net::udp_rx_pending(table, static_cast<size_t>(slot_a)) ==
               cxc::net::udp_socket_rx_depth,
           "queue full");
    const uint16_t overflow = cxc::net::build_udp_datagram(
        frame, sizeof(frame), from_peer, cxc::net::peer_udp_port, 0x1001, 0x3fff,
        payload, 2);
    expect(!cxc::net::udp_deliver(table, frame, overflow), "overflow drop");
    expect(table.sockets[static_cast<size_t>(slot_a)].dropped == 1,
           "drop counted");

    cxc::net::UdpQueuedDatagram got{};
    expect(cxc::net::udp_recv(table, static_cast<size_t>(slot_a), got),
           "recv head");
    expect(got.dst_port == 0x1001 && got.payload_length == 2 &&
               got.payload[0] == 0xAA && got.payload[1] == 0xBB,
           "recv payload");
    expect(cxc::net::udp_close(table, static_cast<size_t>(slot_a)), "close A");
    expect(!cxc::net::udp_recv(table, static_cast<size_t>(slot_a), got),
           "recv after close");

    // M22: deliver leaves sticky rx_gate; recv_wait drains via non-block recv.
    cxc::net::UdpSocketTable wait_table{};
    cxc::net::udp_sockets_init(wait_table);
    const int wait_slot = cxc::net::udp_bind(wait_table, 0x4D22);
    expect(wait_slot >= 0, "M22 bind");
    const uint16_t wait_frame = cxc::net::build_udp_datagram(
        frame, sizeof(frame), from_peer, cxc::net::peer_udp_port, 0x4D22,
        0x4D22, payload, 2);
    expect(cxc::net::udp_deliver(wait_table, frame, wait_frame),
           "M22 deliver");
    cxc::net::UdpQueuedDatagram waited{};
    expect(cxc::net::udp_recv_wait(wait_table, static_cast<size_t>(wait_slot),
                                   waited),
           "M22 recv_wait with pending");
    expect(waited.dst_port == 0x4D22 && waited.payload_length == 2,
           "M22 waited payload");
    expect(!cxc::net::udp_recv_wait(wait_table, static_cast<size_t>(wait_slot),
                                    waited),
           "M22 empty recv_wait fails on host stub");

    // M23: send from A, deliver into B (roles swapped for arrival).
    cxc::net::UdpSocketTable tx_table{};
    cxc::net::udp_sockets_init(tx_table);
    const int tx_slot = cxc::net::udp_bind(tx_table, 0x4D23);
    const int rx_slot = cxc::net::udp_bind(tx_table, 0x4D24);
    expect(tx_slot >= 0 && rx_slot >= 0, "M23 bind pair");
    uint8_t tx_payload[3] = {0x53, 0x45, 0x4E};
    uint8_t tx_frame[128];
    const cxc::net::UdpWireEndpoints to_peer{nic_mac, peer_mac, nic_ipv4,
                                             peer_ipv4};
    const uint16_t tx_len = cxc::net::udp_send(
        tx_table, static_cast<size_t>(tx_slot), tx_frame, sizeof(tx_frame),
        to_peer, 0x4D24, 0x5323, tx_payload, 3);
    expect(tx_len == 60, "M23 send pads to 60");
    cxc::net::UdpDatagramView tx_view{};
    expect(cxc::net::parse_udp_datagram(tx_frame, tx_len, tx_view),
           "M23 parse sent");
    expect(tx_view.src_port == 0x4D23 && tx_view.dst_port == 0x4D24,
           "M23 send ports");
    // Arrival at peer: rebuild with peer as local for deliver into 0x4D24.
    const cxc::net::UdpWireEndpoints as_arrival{peer_mac, nic_mac, peer_ipv4,
                                                nic_ipv4};
    const uint16_t arrival = cxc::net::build_udp_datagram(
        tx_frame, sizeof(tx_frame), as_arrival, 0x4D23, 0x4D24, 0x5323,
        tx_payload, 3);
    expect(cxc::net::udp_deliver(tx_table, tx_frame, arrival),
           "M23 deliver sent payload");
    cxc::net::UdpQueuedDatagram rx_got{};
    expect(cxc::net::udp_recv(tx_table, static_cast<size_t>(rx_slot), rx_got),
           "M23 recv after send");
    expect(rx_got.src_port == 0x4D23 && rx_got.payload_length == 3 &&
               rx_got.payload[0] == 0x53,
           "M23 send/recv payload");
    expect(cxc::net::udp_send(tx_table, static_cast<size_t>(tx_slot), tx_frame,
                              sizeof(tx_frame), to_peer, 0, 1, tx_payload,
                              3) == 0,
           "M23 reject dst port 0");
}

void test_ring_model() {
    expect(cxc::net::udp_rx_descriptor(0) == 3, "first RX descriptor");
    expect(cxc::net::udp_expected_head(0) == 4, "first expected head");
    expect(cxc::net::udp_recycle_source(0) == 0, "first recycle source");
    expect(cxc::net::udp_recycle_target(0) == 7, "first recycle target");

    const auto last = cxc::net::udp_recycle_step(9);
    expect(last.rx_descriptor == 4, "tenth RX descriptor");
    expect(last.expected_head == 5, "final head 5");
    expect(last.expected_rx_tail == 1, "final RDT 1");

    // Ten steps: wrap counts match M13 contract (heads wrap once, RX tails twice).
    uint32_t rx_head_wraps = 0;
    uint32_t rx_tail_wraps = 0;
    for (size_t index = 0; index < 10; ++index) {
        const auto step = cxc::net::udp_recycle_step(index);
        if (step.expected_head == 0) {
            ++rx_head_wraps;
        }
        if (step.expected_rx_tail == 0) {
            ++rx_tail_wraps;
        }
    }
    expect(rx_head_wraps == 1, "one RX head wrap across ten datagrams");
    expect(rx_tail_wraps == 2, "two RX tail wraps across ten datagrams");

    // M27: leave-one-empty TX free/full.
    expect(cxc::net::tx_ring_free(0, 0) == 7, "empty ring has 7 free");
    expect(!cxc::net::tx_ring_full(0, 0), "empty ring is not full");
    expect(cxc::net::tx_ring_free(6, 6) == 7, "empty at 6 has 7 free");
    expect(cxc::net::tx_ring_free(6, 5) == 0, "7 outstanding → full");
    expect(cxc::net::tx_ring_full(6, 5), "full when free is 0");
    expect(cxc::net::tx_ring_free(0, 7) == 0, "full wrapping tdt");
    expect(cxc::net::tx_ring_free(3, 0) == 2, "partial fill free slots");
}

void test_ring_long_stability() {
    uint32_t rx_head_wraps = 0;
    uint32_t rx_tail_wraps = 0;
    for (size_t index = 0; index < 256; ++index) {
        const auto step = cxc::net::udp_recycle_step(index);
        if (step.expected_head == 0) {
            ++rx_head_wraps;
        }
        if (step.expected_rx_tail == 0) {
            ++rx_tail_wraps;
        }
        // Continuing past M13: index 10 must keep modular consistency.
        if (index == 10) {
            expect(step.rx_descriptor == 5, "stress start RX descriptor");
            expect(step.expected_head == 6, "stress start head");
            expect(step.expected_rx_tail == 2, "stress start RDT");
        }
    }
    expect(rx_head_wraps == 32, "256 datagrams: 32 RX head wraps");
    expect(rx_tail_wraps == 32, "256 datagrams: 32 RX tail wraps");
    const auto end = cxc::net::udp_recycle_step(255);
    expect(end.expected_head == (4 + 255) % 8, "final stress head");
    expect(end.expected_rx_tail == 255 % 8, "final stress RDT");
}

}  // namespace

int main() {
    test_checksum_roundtrip();
    test_odd_length_checksum();
    test_arp();
    test_icmp();
    test_udp();
    test_udp_datagram_api();
    test_udp_socket_api();
    test_ring_model();
    test_ring_long_stability();
    if (failures != 0) {
        std::fprintf(stderr, "%d host net protocol test(s) failed\n", failures);
        return 1;
    }
    std::puts("host net protocol tests PASS");
    return 0;
}
