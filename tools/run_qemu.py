#!/usr/bin/env python3
"""Boot QEMU/OVMF and validate raw DMA, ARP, ICMP and UDP/ring evidence.

Thin CLI entry that re-exports protocol helpers for contract_test compatibility.
Process management lives in qemu_runner; peer/validators in network_peer.
"""

from __future__ import annotations

from network_peer import (  # noqa: F401
    DatagramPeer,
    ICMP_PACKET_SIZE,
    NIC_IP,
    NIC_MAC,
    NIC_UDP_PORT,
    PACKET_SIZE,
    PEER_IP,
    PEER_MAC,
    PEER_UDP_PORT,
    PROTOCOL_READY_MARKER,
    READY_MARKER,
    UDP_FAULT_KINDS,
    UDP_PAYLOAD_SIZES,
    UDP_PAYLOAD_TOTAL,
    UDP_READY_MARKER,
    UDP_STRESS_DATAGRAMS,
    UDP_STRESS_PAYLOAD_TOTAL,
    UDP_STRESS_READY_MARKER,
    apply_udp_fault,
    build_arp_request,
    build_icmp_echo_request,
    build_request,
    build_udp_echo_request,
    get16,
    internet_checksum,
    put16,
    reseal_udp_checksum,
    udp_checksum,
    udp_payload,
    valid_arp_reply,
    valid_icmp_reply,
    valid_response,
    valid_udp_reply,
)
from qemu_runner import find_firmware, main  # noqa: F401

if __name__ == "__main__":
    raise SystemExit(main())
