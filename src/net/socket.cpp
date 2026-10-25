#include <cxc/net/socket.hpp>

#include <cxc/net/datagram.hpp>
#include <cxc/net/proto.hpp>

extern "C" void* memset(void*, int, size_t);

namespace cxc::net {
namespace {

bool slot_in_range(size_t slot) {
    return slot < udp_socket_slots;
}

UdpSocket* find_by_port(UdpSocketTable& table, uint16_t port) {
    for (size_t index = 0; index < udp_socket_slots; ++index) {
        UdpSocket& socket = table.sockets[index];
        if (socket.used && socket.local_port == port) {
            return &socket;
        }
    }
    return nullptr;
}

}  // namespace

void udp_sockets_init(UdpSocketTable& table) {
    memset(&table, 0, sizeof(table));
}

int udp_bind(UdpSocketTable& table, uint16_t local_port) {
    if (local_port == 0) {
        return -1;
    }
    if (find_by_port(table, local_port) != nullptr) {
        return -1;
    }
    for (size_t index = 0; index < udp_socket_slots; ++index) {
        UdpSocket& socket = table.sockets[index];
        if (!socket.used) {
            socket.used = true;
            socket.local_port = local_port;
            socket.rx_count = 0;
            socket.rx_head = 0;
            socket.rx_tail = 0;
            socket.dropped = 0;
            scheduler::event_init(socket.rx_gate);
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool udp_close(UdpSocketTable& table, size_t slot) {
    if (!slot_in_range(slot) || !table.sockets[slot].used) {
        return false;
    }
    memset(&table.sockets[slot], 0, sizeof(UdpSocket));
    return true;
}

bool udp_deliver(UdpSocketTable& table, const uint8_t* frame,
                 uint16_t length) {
    UdpDatagramView view{};
    if (!parse_udp_datagram(frame, length, view)) {
        return false;
    }
    UdpSocket* socket = find_by_port(table, view.dst_port);
    if (socket == nullptr) {
        return false;
    }
    if (view.payload_length > udp_socket_payload_cap ||
        socket->rx_count >= udp_socket_rx_depth) {
        ++socket->dropped;
        return false;
    }
    UdpQueuedDatagram& entry = socket->rx[socket->rx_tail];
    entry.src_port = view.src_port;
    entry.dst_port = view.dst_port;
    entry.identification = view.identification;
    entry.payload_length = view.payload_length;
    if (view.payload_length != 0) {
        bytes_copy(entry.payload, view.payload, view.payload_length);
    }
    socket->rx_tail =
        static_cast<uint16_t>((socket->rx_tail + 1) % udp_socket_rx_depth);
    ++socket->rx_count;
    // Sticky signal so a waiter that races after an empty recv still wakes.
    (void)scheduler::signal(socket->rx_gate);
    return true;
}

bool udp_recv(UdpSocketTable& table, size_t slot, UdpQueuedDatagram& out) {
    if (!slot_in_range(slot) || !table.sockets[slot].used ||
        table.sockets[slot].rx_count == 0) {
        return false;
    }
    UdpSocket& socket = table.sockets[slot];
    out = socket.rx[socket.rx_head];
    socket.rx_head =
        static_cast<uint16_t>((socket.rx_head + 1) % udp_socket_rx_depth);
    --socket.rx_count;
    return true;
}

bool udp_recv_wait(UdpSocketTable& table, size_t slot,
                   UdpQueuedDatagram& out) {
    if (!slot_in_range(slot) || !table.sockets[slot].used) {
        return false;
    }
    for (;;) {
        if (udp_recv(table, slot, out)) {
            return true;
        }
        if (!scheduler::wait(table.sockets[slot].rx_gate)) {
            return false;
        }
    }
}

uint16_t udp_rx_pending(const UdpSocketTable& table, size_t slot) {
    if (!slot_in_range(slot) || !table.sockets[slot].used) {
        return 0;
    }
    return table.sockets[slot].rx_count;
}

uint16_t udp_send(UdpSocketTable& table, size_t slot, uint8_t* buffer,
                  uint16_t capacity, const UdpWireEndpoints& endpoints,
                  uint16_t dst_port, uint16_t identification,
                  const uint8_t* payload, uint16_t payload_length) {
    if (!slot_in_range(slot) || !table.sockets[slot].used || buffer == nullptr ||
        dst_port == 0 || payload_length > udp_socket_payload_cap ||
        (payload_length > 0 && payload == nullptr)) {
        return 0;
    }
    return build_udp_datagram(buffer, capacity, endpoints,
                              table.sockets[slot].local_port, dst_port,
                              identification, payload, payload_length);
}

bool udp_socket_contract_ok() {
    UdpSocketTable table{};
    udp_sockets_init(table);
    const int slot = udp_bind(table, nic_udp_port);
    if (slot < 0) {
        return false;
    }

    static constexpr uint8_t local_mac[6] = {0x52, 0x54, 0x00, 0x43, 0x58, 0x43};
    static constexpr uint8_t remote_mac[6] = {0x02, 0x43, 0x58, 0x43, 0x11,
                                             0x01};
    static constexpr uint8_t local_ipv4[4] = {10, 0, 12, 2};
    static constexpr uint8_t remote_ipv4[4] = {10, 0, 12, 1};
    constexpr uint8_t payload[] = {0x4D, 0x32, 0x31};
    constexpr uint16_t payload_length = 3;
    constexpr uint16_t identification = 0x4D21;

    uint8_t frame[64];
    const UdpWireEndpoints from_peer{remote_mac, local_mac, remote_ipv4,
                                     local_ipv4};
    const uint16_t built = build_udp_datagram(
        frame, sizeof(frame), from_peer, peer_udp_port, nic_udp_port,
        identification, payload, payload_length);
    if (built == 0 || !udp_deliver(table, frame, built)) {
        return false;
    }
    if (udp_rx_pending(table, static_cast<size_t>(slot)) != 1) {
        return false;
    }

    UdpQueuedDatagram received{};
    if (!udp_recv(table, static_cast<size_t>(slot), received)) {
        return false;
    }
    if (received.src_port != peer_udp_port ||
        received.dst_port != nic_udp_port ||
        received.identification != identification ||
        received.payload_length != payload_length) {
        return false;
    }
    for (uint16_t index = 0; index < payload_length; ++index) {
        if (received.payload[index] != payload[index]) {
            return false;
        }
    }
    if (udp_rx_pending(table, static_cast<size_t>(slot)) != 0) {
        return false;
    }

    // M23: send from bound nic port toward peer; parse must see src=nic.
    constexpr uint8_t send_payload[] = {0x4D, 0x32, 0x33};
    const UdpWireEndpoints to_peer{local_mac, remote_mac, local_ipv4,
                                   remote_ipv4};
    const uint16_t sent = udp_send(table, static_cast<size_t>(slot), frame,
                                   sizeof(frame), to_peer, peer_udp_port,
                                   0x4D23, send_payload, sizeof(send_payload));
    if (sent == 0) {
        return false;
    }
    UdpDatagramView sent_view{};
    if (!parse_udp_datagram(frame, sent, sent_view) ||
        sent_view.src_port != nic_udp_port ||
        sent_view.dst_port != peer_udp_port ||
        sent_view.payload_length != sizeof(send_payload)) {
        return false;
    }
    for (uint16_t index = 0; index < sizeof(send_payload); ++index) {
        if (sent_view.payload[index] != send_payload[index]) {
            return false;
        }
    }
    if (udp_send(table, static_cast<size_t>(slot), frame, sizeof(frame),
                 to_peer, 0, 1, send_payload, sizeof(send_payload)) != 0) {
        return false;  // dst port 0 rejected
    }

    if (udp_bind(table, nic_udp_port) >= 0) {
        return false;
    }
    if (!udp_close(table, static_cast<size_t>(slot))) {
        return false;
    }
    return udp_bind(table, nic_udp_port) >= 0;
}

}  // namespace cxc::net
