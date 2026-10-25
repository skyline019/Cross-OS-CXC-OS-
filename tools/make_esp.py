#!/usr/bin/env python3
"""Create a deterministic MBR + FAT32 EFI System Partition using only stdlib."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

SECTOR_SIZE = 512
PARTITION_LBA = 2048
PARTITION_SECTORS = 131072  # 64 MiB
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 32
FAT_COUNT = 2
EOC = 0x0FFFFFFF


def directory_entry(name: bytes, attributes: int, cluster: int, size: int = 0) -> bytes:
    if len(name) != 11:
        raise ValueError(f"not an 8.3 directory name: {name!r}")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    # Fixed 1980-01-01 timestamps keep the image reproducible.
    struct.pack_into("<H", entry, 14, 0)
    struct.pack_into("<H", entry, 16, 0x21)
    struct.pack_into("<H", entry, 18, 0x21)
    struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 22, 0)
    struct.pack_into("<H", entry, 24, 0x21)
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def directory_cluster(entries: list[bytes], cluster_size: int) -> bytes:
    data = b"".join(entries)
    if len(data) > cluster_size:
        raise ValueError("directory exceeds one cluster")
    return data.ljust(cluster_size, b"\0")


def chain_clusters(fat: list[int], first: int, count: int) -> None:
    last = first + count - 1
    for cluster in range(first, last + 1):
        fat[cluster] = cluster + 1 if cluster != last else EOC


def build_image(
    kernel: Path,
    output: Path,
    extras: list[tuple[bytes, Path]] | None = None,
) -> None:
    kernel_data = kernel.read_bytes()
    if not kernel_data:
        raise ValueError("kernel image is empty")

    extra_files: list[tuple[bytes, bytes]] = []
    for name, path in extras or []:
        if len(name) != 11:
            raise ValueError(f"extra name must be 8.3: {name!r}")
        payload = path.read_bytes()
        if not payload:
            raise ValueError(f"extra ESP file is empty: {path}")
        extra_files.append((name, payload))

    fat_sectors = 1
    for _ in range(32):
        data_sectors = PARTITION_SECTORS - RESERVED_SECTORS - FAT_COUNT * fat_sectors
        cluster_count = data_sectors // SECTORS_PER_CLUSTER
        required = math.ceil((cluster_count + 2) * 4 / SECTOR_SIZE)
        # The integer layout can alternate between two adjacent values. Any
        # size that is at least the next required value is safe and stable.
        if required <= fat_sectors:
            break
        fat_sectors = required
    else:
        raise RuntimeError("FAT layout did not converge")

    data_start_sector = RESERVED_SECTORS + FAT_COUNT * fat_sectors
    cluster_size = SECTOR_SIZE * SECTORS_PER_CLUSTER
    if cluster_count < 65525:
        raise ValueError("volume is too small for FAT32")
    if cluster_count + 2 >= 0x0FFFFFF5:
        raise ValueError("volume has too many FAT32 clusters")

    kernel_cluster_count = max(1, math.ceil(len(kernel_data) / cluster_size))
    first_kernel_cluster = 5
    last_used_cluster = first_kernel_cluster + kernel_cluster_count - 1

    placed: list[tuple[bytes, bytes, int, int]] = []
    next_cluster = last_used_cluster + 1
    for name, payload in extra_files:
        count = max(1, math.ceil(len(payload) / cluster_size))
        first = next_cluster
        placed.append((name, payload, first, count))
        last_used_cluster = first + count - 1
        next_cluster = last_used_cluster + 1
    if last_used_cluster > cluster_count + 1:
        raise ValueError("ESP contents do not fit in the volume")

    fat = [0] * (fat_sectors * SECTOR_SIZE // 4)
    fat[0] = 0x0FFFFFF8
    fat[1] = 0xFFFFFFFF
    fat[2] = EOC  # Root directory.
    fat[3] = EOC  # /EFI
    fat[4] = EOC  # /EFI/BOOT
    chain_clusters(fat, first_kernel_cluster, kernel_cluster_count)
    for _, _, first, count in placed:
        chain_clusters(fat, first, count)
    fat_data = struct.pack(f"<{len(fat)}I", *fat)

    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[0:3] = b"\xEB\x58\x90"
    boot_sector[3:11] = b"CXCOS   "
    struct.pack_into("<H", boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot_sector, 14, RESERVED_SECTORS)
    boot_sector[16] = FAT_COUNT
    struct.pack_into("<H", boot_sector, 17, 0)
    struct.pack_into("<H", boot_sector, 19, 0)
    boot_sector[21] = 0xF8
    struct.pack_into("<H", boot_sector, 22, 0)
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 255)
    struct.pack_into("<I", boot_sector, 28, PARTITION_LBA)
    struct.pack_into("<I", boot_sector, 32, PARTITION_SECTORS)
    struct.pack_into("<I", boot_sector, 36, fat_sectors)
    struct.pack_into("<H", boot_sector, 40, 0)
    struct.pack_into("<H", boot_sector, 42, 0)
    struct.pack_into("<I", boot_sector, 44, 2)  # Root cluster.
    struct.pack_into("<H", boot_sector, 48, 1)  # FSInfo sector.
    struct.pack_into("<H", boot_sector, 50, 6)  # Backup boot sector.
    boot_sector[64] = 0x80
    boot_sector[66] = 0x29
    struct.pack_into("<I", boot_sector, 68, 0x43584331)
    boot_sector[71:82] = b"CXC OS ESP "
    boot_sector[82:90] = b"FAT32   "
    struct.pack_into("<H", boot_sector, 510, 0xAA55)

    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, cluster_count - (last_used_cluster - 1))
    struct.pack_into("<I", fsinfo, 492, last_used_cluster + 1)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)

    mbr = bytearray(SECTOR_SIZE)
    mbr[446] = 0x00
    mbr[447:450] = b"\x00\x02\x00"
    mbr[450] = 0xEF
    mbr[451:454] = b"\xFF\xFF\xFF"
    struct.pack_into("<I", mbr, 454, PARTITION_LBA)
    struct.pack_into("<I", mbr, 458, PARTITION_SECTORS)
    struct.pack_into("<H", mbr, 510, 0xAA55)

    root = directory_cluster(
        [directory_entry(b"EFI        ", 0x10, 3)], cluster_size
    )
    efi = directory_cluster(
        [
            directory_entry(b".          ", 0x10, 3),
            directory_entry(b"..         ", 0x10, 2),
            directory_entry(b"BOOT       ", 0x10, 4),
        ],
        cluster_size,
    )
    boot_entries = [
        directory_entry(b".          ", 0x10, 4),
        directory_entry(b"..         ", 0x10, 3),
        directory_entry(
            b"BOOTX64 EFI", 0x20, first_kernel_cluster, len(kernel_data)
        ),
    ]
    for name, payload, first, _count in placed:
        boot_entries.append(directory_entry(name, 0x20, first, len(payload)))
    boot = directory_cluster(boot_entries, cluster_size)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as image:
        image.truncate((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)

        def write_sector(sector: int, data: bytes) -> None:
            image.seek(sector * SECTOR_SIZE)
            image.write(data)

        def write_cluster(cluster: int, data: bytes) -> None:
            sector = PARTITION_LBA + data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER
            write_sector(sector, data)

        def write_file_clusters(first: int, count: int, payload: bytes) -> None:
            for index in range(count):
                chunk = payload[index * cluster_size : (index + 1) * cluster_size]
                write_cluster(first + index, chunk.ljust(cluster_size, b"\0"))

        write_sector(0, mbr)
        write_sector(PARTITION_LBA, boot_sector)
        write_sector(PARTITION_LBA + 1, fsinfo)
        write_sector(PARTITION_LBA + 6, boot_sector)
        write_sector(PARTITION_LBA + 7, fsinfo)

        first_fat_sector = PARTITION_LBA + RESERVED_SECTORS
        write_sector(first_fat_sector, fat_data)
        write_sector(first_fat_sector + fat_sectors, fat_data)

        write_cluster(2, root)
        write_cluster(3, efi)
        write_cluster(4, boot)
        write_file_clusters(first_kernel_cluster, kernel_cluster_count, kernel_data)
        for _name, payload, first, count in placed:
            write_file_clusters(first, count, payload)

    extra_note = ""
    if placed:
        parts = [
            f"{len(payload)}B {name.decode(errors='replace').strip()}"
            for name, payload, _first, _count in placed
        ]
        extra_note = ", extras=[" + "; ".join(parts) + "]"
    print(
        f"created {output} ({len(kernel_data)} byte kernel, "
        f"{kernel_cluster_count} kernel clusters{extra_note}, "
        f"{fat_sectors} sectors/FAT)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--extra",
        action="append",
        nargs=2,
        metavar=("NAME11", "PATH"),
        default=[],
        help="FAT 8.3 name (exactly 11 ASCII chars) and file path; repeatable",
    )
    arguments = parser.parse_args()
    extras: list[tuple[bytes, Path]] = []
    for name_text, path_text in arguments.extra:
        name = name_text.encode("ascii")
        if len(name) != 11:
            raise SystemExit(
                f"--extra name must be exactly 11 ASCII bytes, got {len(name)}: {name_text!r}"
            )
        extras.append((name, Path(path_text)))
    build_image(arguments.kernel, arguments.output, extras=extras)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
