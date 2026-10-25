#include <cxc/virtio_blk.hpp>

#include <cxc/cpu.hpp>
#include <cxc/pci.hpp>
#include <cxc/pmm.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" int memcmp(const void* left, const void* right, size_t size);
extern "C" void* memcpy(void* destination, const void* source, size_t size);
extern "C" void* memset(void* destination, int value, size_t size);

namespace cxc::virtio_blk {
namespace {

constexpr uint16_t virtio_vendor = 0x1af4;
constexpr uint16_t virtio_blk_transitional = 0x1001;
constexpr uint16_t virtio_blk_modern = 0x1042;
constexpr uint8_t pci_cap_vendor = 0x09;
constexpr uint8_t virtio_pci_cap_common = 1;
constexpr uint8_t virtio_pci_cap_notify = 2;
constexpr uint8_t virtio_pci_cap_device = 4;

constexpr uint8_t status_acknowledge = 1;
constexpr uint8_t status_driver = 2;
constexpr uint8_t status_driver_ok = 4;
constexpr uint8_t status_features_ok = 8;
constexpr uint8_t status_failed = 128;

constexpr uint64_t feature_version_1 = uint64_t{1} << 32;
constexpr uint32_t blk_t_in = 0;
constexpr uint32_t blk_t_out = 1;
constexpr uint8_t blk_s_ok = 0;

constexpr uint16_t desc_next = 1;
constexpr uint16_t desc_write = 2;
constexpr uint16_t queue_size = 8;
constexpr uint32_t sector_bytes = sector_size;

constexpr uint16_t command_memory = 1 << 1;
constexpr uint16_t command_bus_master = 1 << 2;
constexpr uint16_t command_intx_disable = 1 << 10;

// Keep clear of EDU / e1000e MSI-X / e1000e CSR VA windows.
constexpr uint64_t va_common = 0xffffffff40030000ull;
constexpr uint64_t va_notify = 0xffffffff40031000ull;
constexpr uint64_t va_device = 0xffffffff40032000ull;
constexpr uint64_t dma_limit = uint64_t{1} << 32;

constexpr char expected_magic[8] = {'C', 'X', 'C', 'B', 'L', 'K', '0', '1'};
constexpr char rw_pattern_tag[8] = {'C', 'X', 'C', 'R', 'W', '0', '7', '6'};

struct VirtioPciCap {
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t id;
    uint8_t padding[2];
    uint32_t offset;
    uint32_t length;
};

struct VirtqDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[queue_size];
};

struct VirtqUsedElem {
    uint32_t id;
    uint32_t len;
};

struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[queue_size];
};

struct BlkReqHeader {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct DeviceState {
    bool ready;
    const pci::Device* pci_device;
    uint16_t original_command;
    uint64_t common;
    uint64_t notify;
    uint64_t devcfg;
    uint32_t notify_mult;
    uint16_t notify_off;
    uint64_t capacity;
    uint64_t queue_phys;
    uint64_t io_phys;
    VirtqDesc* desc;
    VirtqAvail* avail;
    VirtqUsed* used;
    uint8_t* io;
};

ProbeResult last_result{};
uint32_t last_failure = 0;
DeviceState device{};

uint8_t mmio8(uint64_t base, uint32_t offset) {
    return *reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(base + offset));
}

uint16_t mmio16(uint64_t base, uint32_t offset) {
    return *reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(base + offset));
}

uint32_t mmio32(uint64_t base, uint32_t offset) {
    return *reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset));
}

uint64_t mmio64(uint64_t base, uint32_t offset) {
    return *reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(base + offset));
}

void mmio_write8(uint64_t base, uint32_t offset, uint8_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(base + offset)) =
        value;
}

void mmio_write16(uint64_t base, uint32_t offset, uint16_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(base + offset)) = value;
}

void mmio_write32(uint64_t base, uint32_t offset, uint32_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset)) = value;
}

void mmio_write64(uint64_t base, uint32_t offset, uint64_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(base + offset)) = value;
}

bool bar_physical(const pci::Device& pci_dev, uint8_t bar_index,
                  uint64_t& out_physical) {
    if (bar_index > 5) {
        return false;
    }
    const uint32_t low = pci::read_bar(pci_dev, bar_index);
    if ((low & 1u) != 0) {
        return false;
    }
    out_physical = low & 0xfffffff0u;
    if ((low & 6u) == 4u) {
        if (bar_index >= 5) {
            return false;
        }
        const uint32_t high =
            pci::read_bar(pci_dev, static_cast<uint8_t>(bar_index + 1));
        out_physical |= static_cast<uint64_t>(high) << 32;
    }
    return out_physical != 0;
}

bool map_cap_window(const pci::Device& pci_dev, const VirtioPciCap& cap,
                    uint64_t virtual_base, uint64_t& out_va) {
    uint64_t bar_phys = 0;
    if (!bar_physical(pci_dev, cap.bar, bar_phys)) {
        return false;
    }
    const uint64_t page_phys = (bar_phys + cap.offset) & ~uint64_t{0xfff};
    const uint32_t page_off =
        static_cast<uint32_t>((bar_phys + cap.offset) & 0xfff);
    if (vmm::is_mapped(virtual_base)) {
        return false;
    }
    if (!vmm::map_device_page(page_phys, virtual_base)) {
        return false;
    }
    out_va = virtual_base + page_off;
    return true;
}

bool read_vendor_cap(const pci::Device& pci_dev, uint8_t offset,
                     VirtioPciCap& cap) {
    if (offset < 0x40) {
        return false;
    }
    const uint32_t w0 = pci::config_read32(pci_dev, offset);
    const uint32_t w1 =
        pci::config_read32(pci_dev, static_cast<uint8_t>(offset + 4));
    const uint32_t w2 =
        pci::config_read32(pci_dev, static_cast<uint8_t>(offset + 8));
    const uint32_t w3 =
        pci::config_read32(pci_dev, static_cast<uint8_t>(offset + 12));
    cap.cap_vndr = static_cast<uint8_t>(w0);
    cap.cap_next = static_cast<uint8_t>(w0 >> 8);
    cap.cap_len = static_cast<uint8_t>(w0 >> 16);
    cap.cfg_type = static_cast<uint8_t>(w0 >> 24);
    cap.bar = static_cast<uint8_t>(w1);
    cap.id = static_cast<uint8_t>(w1 >> 8);
    cap.padding[0] = static_cast<uint8_t>(w1 >> 16);
    cap.padding[1] = static_cast<uint8_t>(w1 >> 24);
    cap.offset = w2;
    cap.length = w3;
    return cap.cap_vndr == pci_cap_vendor && cap.cap_len >= 16;
}

bool find_virtio_device(const pci::Device*& out) {
    out = nullptr;
    for (size_t index = 0; index < pci::device_count(); ++index) {
        const pci::Device& candidate = pci::devices()[index];
        if (candidate.vendor_id != virtio_vendor) {
            continue;
        }
        if (candidate.device_id == virtio_blk_transitional ||
            candidate.device_id == virtio_blk_modern) {
            out = &candidate;
            return true;
        }
    }
    return false;
}

bool submit_request(uint32_t type, uint64_t sector, uint8_t* data_va,
                    uint64_t data_phys, bool data_device_write, uint8_t* status_byte,
                    uint32_t& failure) {
    failure = 0;
    if (!device.ready) {
        failure = 50;
        return false;
    }
    auto* header = reinterpret_cast<BlkReqHeader*>(device.io);
    header->type = type;
    header->reserved = 0;
    header->sector = sector;
    *status_byte = 0xff;

    const uint64_t header_phys = device.io_phys;
    const uint64_t status_phys =
        device.io_phys + 64 + sector_bytes;

    device.desc[0].addr = header_phys;
    device.desc[0].len = sizeof(BlkReqHeader);
    device.desc[0].flags = desc_next;
    device.desc[0].next = 1;
    device.desc[1].addr = data_phys;
    device.desc[1].len = sector_bytes;
    device.desc[1].flags =
        static_cast<uint16_t>(desc_next | (data_device_write ? desc_write : 0));
    device.desc[1].next = 2;
    device.desc[2].addr = status_phys;
    device.desc[2].len = 1;
    device.desc[2].flags = desc_write;
    device.desc[2].next = 0;

    (void)data_va;

    const uint16_t avail_idx = device.avail->idx;
    device.avail->ring[avail_idx % queue_size] = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    device.avail->idx = static_cast<uint16_t>(avail_idx + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    const uint16_t used_before = device.used->idx;
    const uint64_t notify_addr =
        device.notify +
        static_cast<uint64_t>(device.notify_off) * device.notify_mult;
    *reinterpret_cast<volatile uint16_t*>(static_cast<uintptr_t>(notify_addr)) =
        0;

    uint32_t spins = 0;
    while (device.used->idx == used_before) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        ++spins;
        if (spins > 2000000u) {
            failure = 40;
            return false;
        }
        __asm__ volatile("pause");
    }
    if (device.used->ring[used_before % queue_size].id != 0) {
        failure = 41;
        return false;
    }
    return true;
}

bool io_one_sector(uint32_t type, uint64_t lba, void* buffer, bool is_write,
                   uint32_t& failure) {
    if (!device.ready) {
        failure = 50;
        return false;
    }
    if (lba >= device.capacity) {
        failure = 51;
        return false;
    }
    uint8_t* const data = device.io + 64;
    uint8_t* const status_byte = device.io + 64 + sector_bytes;
    const uint64_t data_phys = device.io_phys + 64;
    if (is_write) {
        memcpy(data, buffer, sector_bytes);
    } else {
        memset(data, 0, sector_bytes);
    }
    if (!submit_request(type, lba, data, data_phys, !is_write, status_byte,
                        failure)) {
        return false;
    }
    if (*status_byte != blk_s_ok) {
        failure = 52;
        return false;
    }
    if (!is_write) {
        memcpy(buffer, data, sector_bytes);
    }
    return true;
}

}  // namespace

bool is_ready() {
    return device.ready;
}

uint64_t capacity_sectors() {
    return device.capacity;
}

uint32_t last_io_failure() {
    return last_failure;
}

bool read_sectors(uint64_t lba, void* buffer, size_t count) {
    last_failure = 0;
    if (buffer == nullptr || count == 0) {
        last_failure = 53;
        return false;
    }
    if (!device.ready) {
        last_failure = 50;
        return false;
    }
    if (lba > device.capacity || count > device.capacity - lba) {
        last_failure = 51;
        return false;
    }
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (size_t index = 0; index < count; ++index) {
        uint32_t failure = 0;
        if (!io_one_sector(blk_t_in, lba + index, bytes + index * sector_bytes,
                           false, failure)) {
            last_failure = failure;
            return false;
        }
    }
    return true;
}

bool write_sectors(uint64_t lba, const void* buffer, size_t count) {
    last_failure = 0;
    if (buffer == nullptr || count == 0) {
        last_failure = 53;
        return false;
    }
    if (!device.ready) {
        last_failure = 50;
        return false;
    }
    if (lba > device.capacity || count > device.capacity - lba) {
        last_failure = 51;
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    for (size_t index = 0; index < count; ++index) {
        uint32_t failure = 0;
        // Need mutable copy for io_one_sector memcpy path.
        uint8_t sector[sector_bytes];
        memcpy(sector, bytes + index * sector_bytes, sector_bytes);
        if (!io_one_sector(blk_t_out, lba + index, sector, true, failure)) {
            last_failure = failure;
            return false;
        }
    }
    return true;
}

bool run_probe(ProbeResult& result) {
    result = ProbeResult{};
    last_result = result;
    last_failure = 0;
    device = DeviceState{};
    result.stage = 1;

    const pci::Device* pci_dev = nullptr;
    if (!find_virtio_device(pci_dev)) {
        result.failure = 1;
        last_result = result;
        return false;
    }
    result.found = 1;
    result.vendor_id = pci_dev->vendor_id;
    result.device_id = pci_dev->device_id;
    result.stage = 2;

    VirtioPciCap common_cap{};
    VirtioPciCap notify_cap{};
    VirtioPciCap device_cap{};
    uint32_t notify_mult = 0;
    bool have_common = false;
    bool have_notify = false;
    bool have_device = false;

    uint16_t status_reg = pci::config_read16(*pci_dev, 0x06);
    if ((status_reg & 0x10) == 0) {
        result.failure = 2;
        last_result = result;
        return false;
    }
    const auto read_byte = [pci_dev](uint8_t offset) {
        return static_cast<uint8_t>(pci::config_read32(*pci_dev, offset) >>
                                    ((offset & 3) * 8));
    };
    uint8_t cap_off = read_byte(0x34) & 0xfc;
    uint32_t vendor_seen = 0;
    for (uint32_t steps = 0; cap_off != 0 && steps < 48; ++steps) {
        if (cap_off < 0x40) {
            break;
        }
        const uint8_t id = read_byte(cap_off);
        const uint8_t next = read_byte(static_cast<uint8_t>(cap_off + 1)) & 0xfc;
        if (id == 0 || next == cap_off) {
            break;
        }
        if (id == pci_cap_vendor) {
            VirtioPciCap cap{};
            if (read_vendor_cap(*pci_dev, cap_off, cap)) {
                ++vendor_seen;
                if (cap.cfg_type == virtio_pci_cap_common) {
                    common_cap = cap;
                    have_common = true;
                } else if (cap.cfg_type == virtio_pci_cap_notify) {
                    notify_cap = cap;
                    have_notify = true;
                    if (cap.cap_len >= 20) {
                        notify_mult = pci::config_read32(
                            *pci_dev, static_cast<uint8_t>(cap_off + 16));
                    }
                } else if (cap.cfg_type == virtio_pci_cap_device) {
                    device_cap = cap;
                    have_device = true;
                }
            }
        }
        cap_off = next;
    }
    if (!have_common) {
        result.failure = vendor_seen == 0 ? 30 : 31;
        last_result = result;
        return false;
    }
    if (!have_notify) {
        result.failure = 32;
        last_result = result;
        return false;
    }
    if (!have_device) {
        result.failure = 33;
        last_result = result;
        return false;
    }
    if (notify_mult == 0) {
        notify_mult = 2;
    }
    result.stage = 3;

    uint64_t common = 0;
    uint64_t notify = 0;
    uint64_t devcfg = 0;
    if (!map_cap_window(*pci_dev, common_cap, va_common, common)) {
        result.failure = 41;
        last_result = result;
        return false;
    }
    if (!map_cap_window(*pci_dev, notify_cap, va_notify, notify)) {
        result.failure = 42;
        last_result = result;
        return false;
    }
    if (!map_cap_window(*pci_dev, device_cap, va_device, devcfg)) {
        result.failure = 43;
        last_result = result;
        return false;
    }
    result.mmio_common = common;
    result.stage = 4;

    const uint16_t original_command = pci::config_read16(*pci_dev, 0x04);
    pci::config_write16(*pci_dev, 0x04,
                        original_command | command_memory | command_bus_master |
                            command_intx_disable);

    mmio_write8(common, 20, 0);
    for (uint32_t i = 0; i < 1000; ++i) {
        __asm__ volatile("pause");
    }
    mmio_write8(common, 20, status_acknowledge | status_driver);

    mmio_write32(common, 0, 0);
    const uint32_t feat0 = mmio32(common, 4);
    mmio_write32(common, 0, 1);
    const uint32_t feat1 = mmio32(common, 4);
    const uint64_t device_features =
        (static_cast<uint64_t>(feat1) << 32) | feat0;
    if ((device_features & feature_version_1) == 0) {
        mmio_write8(common, 20, status_failed);
        result.failure = 5;
        last_result = result;
        return false;
    }
    mmio_write32(common, 8, 1);
    mmio_write32(common, 12, static_cast<uint32_t>(feature_version_1 >> 32));
    mmio_write32(common, 8, 0);
    mmio_write32(common, 12, 0);
    mmio_write8(common, 20,
                status_acknowledge | status_driver | status_features_ok);
    if ((mmio8(common, 20) & status_features_ok) == 0) {
        result.failure = 6;
        last_result = result;
        return false;
    }
    result.features_ok = 1;
    result.stage = 5;

    const uint64_t capacity = mmio64(devcfg, 0);
    result.capacity_sectors = capacity;
    if (capacity == 0) {
        result.failure = 7;
        last_result = result;
        return false;
    }

    const uint64_t queue_pages = pmm::allocate_pages_below(2, dma_limit);
    if (queue_pages == 0) {
        result.failure = 8;
        last_result = result;
        return false;
    }
    auto* const queue_mem =
        reinterpret_cast<uint8_t*>(vmm::direct_map_base + queue_pages);
    memset(queue_mem, 0, 2 * pmm::page_size);

    auto* desc = reinterpret_cast<VirtqDesc*>(queue_mem);
    auto* avail = reinterpret_cast<VirtqAvail*>(
        queue_mem + sizeof(VirtqDesc) * queue_size);
    auto* used = reinterpret_cast<VirtqUsed*>(queue_mem + pmm::page_size);
    const uint64_t desc_phys = queue_pages;
    const uint64_t avail_phys =
        queue_pages + sizeof(VirtqDesc) * queue_size;
    const uint64_t used_phys = queue_pages + pmm::page_size;

    mmio_write16(common, 22, 0);
    mmio_write16(common, 24, queue_size);
    mmio_write16(common, 26, 0xffff);
    mmio_write64(common, 32, desc_phys);
    mmio_write64(common, 40, avail_phys);
    mmio_write64(common, 48, used_phys);
    const uint16_t notify_off = mmio16(common, 30);
    mmio_write16(common, 28, 1);
    result.queue_ok = 1;
    result.stage = 6;

    mmio_write8(common, 20,
                status_acknowledge | status_driver | status_features_ok |
                    status_driver_ok);

    const uint64_t io_pages = pmm::allocate_pages_below(1, dma_limit);
    if (io_pages == 0) {
        pmm::free_pages(queue_pages, 2);
        result.failure = 9;
        last_result = result;
        return false;
    }
    auto* const io = reinterpret_cast<uint8_t*>(vmm::direct_map_base + io_pages);
    memset(io, 0, pmm::page_size);

    device.ready = true;
    device.pci_device = pci_dev;
    device.original_command = original_command;
    device.common = common;
    device.notify = notify;
    device.devcfg = devcfg;
    device.notify_mult = notify_mult;
    device.notify_off = notify_off;
    device.capacity = capacity;
    device.queue_phys = queue_pages;
    device.io_phys = io_pages;
    device.desc = desc;
    device.avail = avail;
    device.used = used;
    device.io = io;

    uint8_t sector[sector_bytes];
    uint32_t failure = 0;
    if (!read_sectors(0, sector, 1)) {
        result.failure = last_failure != 0 ? last_failure : 42;
        device.ready = false;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.read_ok = 1;
    if (memcmp(sector, expected_magic, sizeof(expected_magic)) != 0) {
        result.failure = 43;
        device.ready = false;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.magic_ok = 1;
    result.stage = 7;

    // Edge: OOB LBA must fail at API (no OK status).
    if (read_sectors(capacity, sector, 1)) {
        result.failure = 44;
        device.ready = false;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    if (last_failure != 51) {
        // Also accept device-level reject if somehow past API (legacy path).
        result.failure = 45;
        device.ready = false;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.oob_rejected = 1;
    result.stage = 8;

    // Keep device online — do not tear down (M75 debt fix for A2).
    result.pass = 1;
    result.stage = 9;
    last_result = result;
    (void)failure;
    return true;
}

bool run_rw_probe(ProbeResult& result) {
    // Preserve M75 fields; extend with R/W evidence.
    result = last_result;
    result.stage = 10;
    if (!device.ready || result.pass == 0) {
        result.failure = 60;
        last_result = result;
        return false;
    }
    if (device.capacity < 2) {
        result.failure = 61;
        last_result = result;
        return false;
    }

    uint8_t pattern[sector_bytes];
    memset(pattern, 0xa5, sizeof(pattern));
    memcpy(pattern, rw_pattern_tag, sizeof(rw_pattern_tag));
    for (uint32_t i = 8; i < sector_bytes; ++i) {
        pattern[i] = static_cast<uint8_t>(i ^ 0x5a);
    }

    if (!write_sectors(1, pattern, 1)) {
        result.failure = last_failure != 0 ? last_failure : 62;
        last_result = result;
        return false;
    }
    result.write_ok = 1;
    result.stage = 11;

    uint8_t readback[sector_bytes];
    memset(readback, 0, sizeof(readback));
    if (!read_sectors(1, readback, 1)) {
        result.failure = last_failure != 0 ? last_failure : 63;
        last_result = result;
        return false;
    }
    if (memcmp(readback, pattern, sector_bytes) != 0) {
        result.failure = 64;
        last_result = result;
        return false;
    }
    result.rw_roundtrip_ok = 1;
    result.stage = 12;

    // Edge: OOB write rejected.
    if (write_sectors(device.capacity, pattern, 1)) {
        result.failure = 65;
        last_result = result;
        return false;
    }
    if (last_failure != 51) {
        result.failure = 66;
        last_result = result;
        return false;
    }
    result.oob_write_rejected = 1;

    // LBA0 magic must still be intact after R/W.
    uint8_t sector0[sector_bytes];
    if (!read_sectors(0, sector0, 1) ||
        memcmp(sector0, expected_magic, sizeof(expected_magic)) != 0) {
        result.failure = 67;
        last_result = result;
        return false;
    }

    result.rw_pass = 1;
    result.stage = 13;
    last_result = result;
    return true;
}

const ProbeResult& probe_result() {
    return last_result;
}

}  // namespace cxc::virtio_blk
