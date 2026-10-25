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
constexpr uint8_t blk_s_ok = 0;

constexpr uint16_t desc_next = 1;
constexpr uint16_t desc_write = 2;
constexpr uint16_t queue_size = 8;
constexpr uint32_t sector_bytes = 512;

constexpr uint16_t command_memory = 1 << 1;
constexpr uint16_t command_bus_master = 1 << 2;
constexpr uint16_t command_intx_disable = 1 << 10;

// Fixed device VAs (NX/UC via map_device_page). Keep clear of EDU
// (0x...40001000), e1000e MSI-X (0x...40010000), and e1000e CSR (0x...40020000).
constexpr uint64_t va_common = 0xffffffff40030000ull;
constexpr uint64_t va_notify = 0xffffffff40031000ull;
constexpr uint64_t va_device = 0xffffffff40032000ull;
constexpr uint64_t dma_limit = uint64_t{1} << 32;

constexpr char expected_magic[8] = {'C', 'X', 'C', 'B', 'L', 'K', '0', '1'};

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

ProbeResult last_result{};

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

bool bar_physical(const pci::Device& device, uint8_t bar_index,
                  uint64_t& out_physical) {
    if (bar_index > 5) {
        return false;
    }
    const uint32_t low = pci::read_bar(device, bar_index);
    if ((low & 1u) != 0) {
        return false;  // I/O BAR
    }
    out_physical = low & 0xfffffff0u;
    if ((low & 6u) == 4u) {
        if (bar_index >= 5) {
            return false;
        }
        const uint32_t high = pci::read_bar(device, static_cast<uint8_t>(bar_index + 1));
        out_physical |= static_cast<uint64_t>(high) << 32;
    }
    return out_physical != 0;
}

bool map_cap_window(const pci::Device& device, const VirtioPciCap& cap,
                    uint64_t virtual_base, uint64_t& out_va) {
    uint64_t bar_phys = 0;
    if (!bar_physical(device, cap.bar, bar_phys)) {
        return false;
    }
    const uint64_t page_phys = (bar_phys + cap.offset) & ~uint64_t{0xfff};
    const uint32_t page_off = static_cast<uint32_t>((bar_phys + cap.offset) & 0xfff);
    if (vmm::is_mapped(virtual_base)) {
        // Remap not supported; require unused VA.
        return false;
    }
    if (!vmm::map_device_page(page_phys, virtual_base)) {
        return false;
    }
    out_va = virtual_base + page_off;
    return true;
}

bool read_vendor_cap(const pci::Device& device, uint8_t offset, VirtioPciCap& cap) {
    if (offset < 0x40) {
        return false;
    }
    const uint32_t w0 = pci::config_read32(device, offset);
    const uint32_t w1 = pci::config_read32(device, static_cast<uint8_t>(offset + 4));
    const uint32_t w2 = pci::config_read32(device, static_cast<uint8_t>(offset + 8));
    const uint32_t w3 = pci::config_read32(device, static_cast<uint8_t>(offset + 12));
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

bool submit_read(uint64_t notify, uint32_t notify_mult, uint16_t notify_off,
                 VirtqDesc* desc, VirtqAvail* avail, VirtqUsed* used,
                 uint64_t header_phys, uint64_t data_phys, uint64_t status_phys,
                 uint64_t sector, uint8_t* status_byte, uint32_t& failure) {
    auto* header = reinterpret_cast<BlkReqHeader*>(
        vmm::direct_map_base + header_phys);
    header->type = blk_t_in;
    header->reserved = 0;
    header->sector = sector;
    *status_byte = 0xff;

    desc[0].addr = header_phys;
    desc[0].len = sizeof(BlkReqHeader);
    desc[0].flags = desc_next;
    desc[0].next = 1;
    desc[1].addr = data_phys;
    desc[1].len = sector_bytes;
    desc[1].flags = desc_next | desc_write;
    desc[1].next = 2;
    desc[2].addr = status_phys;
    desc[2].len = 1;
    desc[2].flags = desc_write;
    desc[2].next = 0;

    const uint16_t avail_idx = avail->idx;
    avail->ring[avail_idx % queue_size] = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    avail->idx = static_cast<uint16_t>(avail_idx + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    const uint16_t used_before = used->idx;
    const uint64_t notify_addr =
        notify + static_cast<uint64_t>(notify_off) * notify_mult;
    *reinterpret_cast<volatile uint16_t*>(static_cast<uintptr_t>(notify_addr)) =
        0;

    uint32_t spins = 0;
    while (used->idx == used_before) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        ++spins;
        if (spins > 2000000u) {
            failure = 40;
            return false;
        }
        __asm__ volatile("pause");
    }
    if (used->ring[used_before % queue_size].id != 0) {
        failure = 41;
        return false;
    }
    return true;
}

}  // namespace

bool run_probe(ProbeResult& result) {
    result = ProbeResult{};
    last_result = result;
    result.stage = 1;

    const pci::Device* device = nullptr;
    if (!find_virtio_device(device)) {
        result.failure = 1;
        last_result = result;
        return false;
    }
    result.found = 1;
    result.vendor_id = device->vendor_id;
    result.device_id = device->device_id;
    result.stage = 2;

    VirtioPciCap common_cap{};
    VirtioPciCap notify_cap{};
    VirtioPciCap device_cap{};
    uint32_t notify_mult = 0;
    bool have_common = false;
    bool have_notify = false;
    bool have_device = false;

    uint16_t status_reg = pci::config_read16(*device, 0x06);
    if ((status_reg & 0x10) == 0) {
        result.failure = 2;
        last_result = result;
        return false;
    }
    // Match pci::find_capability: dword-align pointers and byte-extract fields.
    const auto read_byte = [device](uint8_t offset) {
        return static_cast<uint8_t>(pci::config_read32(*device, offset) >>
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
            if (read_vendor_cap(*device, cap_off, cap)) {
                ++vendor_seen;
                if (cap.cfg_type == virtio_pci_cap_common) {
                    common_cap = cap;
                    have_common = true;
                } else if (cap.cfg_type == virtio_pci_cap_notify) {
                    notify_cap = cap;
                    have_notify = true;
                    if (cap.cap_len >= 20) {
                        notify_mult = pci::config_read32(
                            *device, static_cast<uint8_t>(cap_off + 16));
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
        // 30 = no vendor caps parsed; 31 = vendor present but no common.
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
    // Virtio: multiplier 0 means densely packed u16 notify slots.
    if (notify_mult == 0) {
        notify_mult = 2;
    }
    result.stage = 3;

    uint64_t common = 0;
    uint64_t notify = 0;
    uint64_t devcfg = 0;
    if (!map_cap_window(*device, common_cap, va_common, common)) {
        result.failure = 41;
        last_result = result;
        return false;
    }
    if (!map_cap_window(*device, notify_cap, va_notify, notify)) {
        result.failure = 42;
        last_result = result;
        return false;
    }
    if (!map_cap_window(*device, device_cap, va_device, devcfg)) {
        result.failure = 43;
        last_result = result;
        return false;
    }
    result.mmio_common = common;
    result.stage = 4;

    const uint16_t original_command = pci::config_read16(*device, 0x04);
    pci::config_write16(*device, 0x04,
                        original_command | command_memory | command_bus_master |
                            command_intx_disable);

    // Reset + acknowledge.
    mmio_write8(common, 18, 0);  // device_status (after msix/num_queues layout)
    // Layout: device_feature_select@0, device_feature@4, driver_feature_select@8,
    // driver_feature@12, msix_config@16, num_queues@18, device_status@20,
    // config_generation@21, queue_select@22, ...
    // Fix: num_queues is u16 at 18, device_status u8 at 20.
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
    mmio_write8(common, 20, status_acknowledge | status_driver | status_features_ok);
    if ((mmio8(common, 20) & status_features_ok) == 0) {
        result.failure = 6;
        last_result = result;
        return false;
    }
    result.features_ok = 1;
    result.stage = 5;

    result.capacity_sectors = mmio64(devcfg, 0);
    if (result.capacity_sectors == 0) {
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
    // Align used to page 2 for device-write separation simplicity.
    auto* used = reinterpret_cast<VirtqUsed*>(queue_mem + pmm::page_size);
    const uint64_t desc_phys = queue_pages;
    const uint64_t avail_phys =
        queue_pages + sizeof(VirtqDesc) * queue_size;
    const uint64_t used_phys = queue_pages + pmm::page_size;

    mmio_write16(common, 22, 0);  // queue_select
    mmio_write16(common, 24, queue_size);
    mmio_write16(common, 26, 0xffff);  // no MSI-X
    mmio_write64(common, 32, desc_phys);
    mmio_write64(common, 40, avail_phys);
    mmio_write64(common, 48, used_phys);
    const uint16_t notify_off = mmio16(common, 30);
    mmio_write16(common, 28, 1);  // queue_enable
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
    const uint64_t header_phys = io_pages;
    const uint64_t data_phys = io_pages + 64;
    const uint64_t status_phys = io_pages + 64 + sector_bytes;
    uint8_t* const status_byte = io + 64 + sector_bytes;
    uint8_t* const data = io + 64;

    uint32_t failure = 0;
    if (!submit_read(notify, notify_mult, notify_off, desc, avail, used,
                     header_phys, data_phys, status_phys, 0, status_byte,
                     failure)) {
        result.failure = failure;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    if (*status_byte != blk_s_ok) {
        result.failure = 42;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.read_ok = 1;
    if (memcmp(data, expected_magic, sizeof(expected_magic)) != 0) {
        result.failure = 43;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.magic_ok = 1;
    result.stage = 7;

    // Edge: OOB LBA must not return OK.
    if (!submit_read(notify, notify_mult, notify_off, desc, avail, used,
                     header_phys, data_phys, status_phys,
                     result.capacity_sectors, status_byte, failure)) {
        result.failure = failure;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    if (*status_byte == blk_s_ok) {
        result.failure = 44;
        pmm::free_pages(io_pages, 1);
        pmm::free_pages(queue_pages, 2);
        last_result = result;
        return false;
    }
    result.oob_rejected = 1;
    result.stage = 8;

    mmio_write16(common, 22, 0);
    mmio_write16(common, 28, 0);
    mmio_write8(common, 20, 0);
    pci::config_write16(*device, 0x04,
                        (original_command & ~command_bus_master) |
                            command_intx_disable);

    pmm::free_pages(io_pages, 1);
    pmm::free_pages(queue_pages, 2);

    result.pass = 1;
    result.stage = 9;
    last_result = result;
    (void)memcmp;
    (void)memcpy;
    return true;
}

const ProbeResult& probe_result() {
    return last_result;
}

}  // namespace cxc::virtio_blk
