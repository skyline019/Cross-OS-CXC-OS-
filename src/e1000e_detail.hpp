#pragma once

#include <cxc/e1000e.hpp>
#include <cxc/net/proto.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::e1000e::detail {

constexpr uint64_t csr_virtual_base = 0xffffffff40020000;
constexpr uint64_t wait_ticks = 1000;

constexpr uint32_t reg_icr = 0x00c0;
constexpr uint32_t reg_ics = 0x00c8;
constexpr uint32_t reg_ctrl = 0x0000;
constexpr uint32_t reg_status = 0x0008;
constexpr uint32_t reg_rctl = 0x0100;
constexpr uint32_t reg_tctl = 0x0400;
constexpr uint32_t reg_tipg = 0x0410;
constexpr uint32_t reg_rxcsum = 0x5000;
constexpr uint32_t reg_rfctl = 0x5008;
constexpr uint32_t reg_mta = 0x5200;
constexpr uint32_t reg_ral0 = 0x5400;
constexpr uint32_t reg_rah0 = 0x5404;
constexpr uint32_t reg_mrqc = 0x5818;
constexpr uint32_t reg_rdbal0 = 0x2800;
constexpr uint32_t reg_rdbah0 = 0x2804;
constexpr uint32_t reg_rdlen0 = 0x2808;
constexpr uint32_t reg_rdh0 = 0x2810;
constexpr uint32_t reg_rdt0 = 0x2818;
constexpr uint32_t reg_rdtr0 = 0x2820;
constexpr uint32_t reg_rxdctl0 = 0x2828;
constexpr uint32_t reg_radv0 = 0x282c;
constexpr uint32_t reg_srrctl0 = 0x280c;
constexpr uint32_t reg_tdbal0 = 0x3800;
constexpr uint32_t reg_tdbah0 = 0x3804;
constexpr uint32_t reg_tdlen0 = 0x3808;
constexpr uint32_t reg_tdh0 = 0x3810;
constexpr uint32_t reg_tdt0 = 0x3818;
constexpr uint32_t reg_tidv0 = 0x3820;
constexpr uint32_t reg_txdctl0 = 0x3828;
constexpr uint32_t reg_tadv0 = 0x382c;
constexpr uint32_t reg_tarc0 = 0x3840;

constexpr uint32_t icr_asserted = 0x80000000;
constexpr uint32_t cause_txqe = 0x00000002;
constexpr uint32_t cause_lsc = 0x00000004;
constexpr uint32_t auxiliary_causes = cause_txqe | cause_lsc;
constexpr uint32_t cause_rxq0 = 0x00100000;
constexpr uint32_t cause_txq0 = 0x00400000;
constexpr uint32_t packet_queue_causes = cause_rxq0 | cause_txq0;

constexpr uint32_t ctrl_slu = 0x00000040;
constexpr uint32_t status_link_up = 0x00000002;
constexpr uint32_t rctl_enable = 0x00000002;
constexpr uint32_t rctl_broadcast = 0x00008000;
constexpr uint32_t rctl_strip_crc = 0x04000000;
constexpr uint32_t tctl_enable = 0x00000002;
constexpr uint32_t tctl_pad_short = 0x00000008;
constexpr uint32_t tctl_retransmit_late = 0x01000000;
constexpr uint32_t queue_enable = 0x02000000;
constexpr uint32_t descriptor_granularity = 0x01000000;
constexpr uint32_t txdctl_count_desc = 0x00400000;
constexpr uint32_t tarc_enable = 0x00000400;
constexpr uint32_t tarc_82574_workaround = 0x04000000;
constexpr uint32_t rah_address_valid = 0x80000000;
constexpr uint8_t tx_command_eop = 0x01;
constexpr uint8_t tx_command_ifcs = 0x02;
constexpr uint8_t tx_command_rs = 0x08;
constexpr uint8_t descriptor_done = 0x01;
constexpr uint8_t rx_descriptor_eop = 0x02;

constexpr size_t descriptor_count = 8;
constexpr size_t rx_buffer_count = descriptor_count - 1;
constexpr uint32_t descriptor_bytes = descriptor_count * 16;
constexpr uint32_t packet_bytes = 60;
constexpr uint16_t icmp_test_frame_bytes = 74;
constexpr uint64_t packet_dma_limit = 0x100000000ull;
constexpr uint8_t peer_mac[6] = {0x02, 0x43, 0x58, 0x43, 0x11, 0x01};
constexpr uint8_t peer_ipv4[4] = {10, 0, 12, 1};
constexpr uint8_t nic_ipv4[4] = {10, 0, 12, 2};
constexpr size_t udp_test_datagrams = 10;
constexpr size_t udp_stress_datagrams = 256;
constexpr uint8_t udp_payload_lengths[udp_test_datagrams] = {
    1, 2, 3, 7, 8, 15, 16, 31, 32, 33,
};
constexpr uint32_t udp_expected_payload_bytes = 148;
constexpr uint32_t udp_expected_frame_bytes = 642;

struct LegacyRxDescriptor {
    uint64_t buffer_address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};
static_assert(sizeof(LegacyRxDescriptor) == 16);

struct LegacyTxDescriptor {
    uint64_t buffer_address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
};
static_assert(sizeof(LegacyTxDescriptor) == 16);

extern TestInfo info;

extern volatile bool packet_active;
extern volatile bool protocol_active;
extern volatile bool udp_active;
extern volatile bool udp_stress_active;
extern volatile bool socket_tx_active;
extern volatile bool socket_tx_bp_active;
extern volatile bool socket_tx_wait_active;
extern volatile bool socket_irq_rx_active;
extern volatile LegacyRxDescriptor* live_rx_ring;
extern volatile LegacyTxDescriptor* live_tx_ring;
extern uint8_t* live_tx_buffer;
extern uint32_t irq_rx_next;
extern uint32_t irq_echo_next;
// BSP datagrams completed (replies); IRQ must not outrun socket RX depth.
extern uint32_t irq_bsp_progress;
extern uint64_t packet_pages[descriptor_count + 2];
extern size_t packet_page_count;
extern uint64_t packet_free_before;
extern bool packet_allocation_attempted;
// M29/M30: RXQ0 MSI-X / TXQ0 / BSP kick delivers validated UDP + Echo.
// from_rx_msi: true when entered from RXQ0 IRQ (handler already tallied RX).
void irq_try_demux_rx(bool from_rx_msi = false);

enum class NetworkPhase {
    Raw,
    Protocol,
    Udp,
    UdpStress,
    UdpSocketTx,
    UdpSocketTxBp,
    UdpSocketTxWait,
};

uint32_t mmio_read(uint32_t offset);
void mmio_write(uint32_t offset, uint32_t value);

void release_packet_pages();
bool prepare_packet_rings(volatile LegacyRxDescriptor*& rx_ring,
                          uint8_t** rx_buffers,
                          volatile LegacyTxDescriptor*& tx_ring,
                          uint8_t*& tx_buffer);

bool valid_request_frame(const uint8_t* frame, uint16_t length);
void fill_rx_guard(uint8_t* buffer);
bool rx_guard_intact(const uint8_t* buffer, uint16_t length);

bool valid_arp_request(const uint8_t* frame, uint16_t length);
uint16_t build_arp_reply(uint8_t* tx_buffer);
bool valid_icmp_echo_request(const uint8_t* frame, uint16_t length);
uint16_t build_icmp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                               uint16_t length);
bool valid_udp_request(const uint8_t* frame, uint16_t length);
uint16_t build_udp_echo_reply(uint8_t* tx_buffer, const uint8_t* request,
                              uint16_t request_length);

uint64_t network_rx_interrupts(NetworkPhase phase);
uint64_t network_tx_interrupts(NetworkPhase phase);
bool wait_for_packet_rx(volatile LegacyRxDescriptor& descriptor,
                        uint64_t first_tick, NetworkPhase phase,
                        uint64_t expected_interrupts);
bool wait_for_packet_tx(volatile LegacyTxDescriptor& descriptor,
                        uint64_t first_tick, NetworkPhase phase,
                        uint64_t expected_interrupts);
bool network_vector_errors();
// Posts one TX descriptor if the ring has a free leave-one-empty slot.
// Does not wait for DD. Returns false when full or arguments are invalid.
bool try_post_tx_frame(volatile LegacyTxDescriptor* tx_ring, uint32_t index,
                       uint16_t length);
// M28: retry try_post until a free slot appears (BSP wait_for_interrupt) or
// timeout. Sets blocked=1 if at least one failed attempt preceded success.
bool post_tx_wait(volatile LegacyTxDescriptor* tx_ring, uint16_t length,
                  uint64_t first_tick, uint32_t& blocked);
bool submit_tx_frame(volatile LegacyTxDescriptor* tx_ring, uint32_t index,
                     uint16_t length, uint64_t first_tick, NetworkPhase phase,
                     uint64_t expected_interrupts);

bool run_packet_phase();

}  // namespace cxc::e1000e::detail
