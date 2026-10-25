#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::net {

// Pure index math for the M13 eight-descriptor / seven-page RX FIFO contract.
// Matches tools/contract_test.py RingModel and e1000e UDP recycle indices.
constexpr size_t ring_descriptor_count = 8;
constexpr size_t ring_rx_buffer_count = ring_descriptor_count - 1;

constexpr uint32_t ring_mod(uint32_t value) {
    return value % static_cast<uint32_t>(ring_descriptor_count);
}

// UDP datagram `index` (0..9) consumes RX/TX descriptor (index + 3) % 8.
constexpr uint32_t udp_rx_descriptor(size_t index) {
    return ring_mod(static_cast<uint32_t>(index + 3));
}

constexpr uint32_t udp_tx_descriptor(size_t index) {
    return udp_rx_descriptor(index);
}

// Oldest consumed page moves from source slot to the previous RDT slot.
constexpr uint32_t udp_recycle_source(size_t index) {
    return ring_mod(static_cast<uint32_t>(index));
}

constexpr uint32_t udp_recycle_target(size_t index) {
    return ring_mod(static_cast<uint32_t>(index + 7));
}

// Hardware head after consuming datagram `index` (starts at RDH=3).
constexpr uint32_t udp_expected_head(size_t index) {
    return ring_mod(static_cast<uint32_t>(index + 4));
}

// RDT after recycling for datagram `index` (starts at RDT=7).
constexpr uint32_t udp_expected_rx_tail(size_t index) {
    return ring_mod(static_cast<uint32_t>(index));
}

struct RingRecycleStep {
    uint32_t rx_descriptor;
    uint32_t tx_descriptor;
    uint32_t recycle_source;
    uint32_t recycle_target;
    uint32_t expected_head;
    uint32_t expected_rx_tail;
};

constexpr RingRecycleStep udp_recycle_step(size_t index) {
    return RingRecycleStep{
        udp_rx_descriptor(index),
        udp_tx_descriptor(index),
        udp_recycle_source(index),
        udp_recycle_target(index),
        udp_expected_head(index),
        udp_expected_rx_tail(index),
    };
}

}  // namespace cxc::net
