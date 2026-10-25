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


def build_image(kernel: Path, output: Path) -> None:
    kernel_data = kernel.read_bytes()
    if not kernel_data:
        raise ValueError("kernel image is empty")

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

    file_cluster_count = max(1, math.ceil(len(kernel_data) / cluster_size))
    first_file_cluster = 5
    last_used_cluster = first_file_cluster + file_cluster_count - 1
    if last_used_cluster > cluster_count + 1:
        raise ValueError("kernel does not fit in the ESP")

    fat = [0] * (fat_sectors * SECTOR_SIZE // 4)
    fat[0] = 0x0FFFFFF8
    fat[1] = 0xFFFFFFFF
    fat[2] = EOC  # Root directory.
    fat[3] = EOC  # /EFI
    fat[4] = EOC  # /EFI/BOOT
    for cluster in range(first_file_cluster, last_used_cluster + 1):
        fat[cluster] = cluster + 1 if cluster != last_used_cluster else EOC
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
    boot = directory_cluster(
        [
            directory_entry(b".          ", 0x10, 4),
            directory_entry(b"..         ", 0x10, 3),
            directory_entry(
                b"BOOTX64 EFI", 0x20, first_file_cluster, len(kernel_data)
            ),
        ],
        cluster_size,
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as image:
        image.truncate((PARTITION_LBA + PARTITION_SECTORS) * SECTOR_SIZE)

        def write_sector(sector: int, data: bytes) -> None:
            image.seek(sector * SECTOR_SIZE)
            image.write(data)

        def write_cluster(cluster: int, data: bytes) -> None:
            sector = PARTITION_LBA + data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER
            write_sector(sector, data)

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
        for index in range(file_cluster_count):
            chunk = kernel_data[index * cluster_size : (index + 1) * cluster_size]
            write_cluster(first_file_cluster + index, chunk.ljust(cluster_size, b"\0"))

    print(
        f"created {output} ({len(kernel_data)} byte kernel, "
        f"{file_cluster_count} file clusters, {fat_sectors} sectors/FAT)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    build_image(arguments.kernel, arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
