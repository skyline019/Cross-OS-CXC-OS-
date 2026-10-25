#include <cxc/vmm.hpp>

#include <cxc/cpu.hpp>
#include <cxc/log.hpp>
#include <cxc/pmm.hpp>
#include <cxc/smp.hpp>
#include <cxc/spinlock.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::vmm {
namespace {

constexpr uint64_t address_mask = 0x000ffffffffff000;
constexpr uint64_t present = uint64_t{1} << 0;
constexpr uint64_t writable = uint64_t{1} << 1;
constexpr uint64_t write_through = uint64_t{1} << 3;
constexpr uint64_t cache_disable = uint64_t{1} << 4;
constexpr uint64_t huge_page = uint64_t{1} << 7;
constexpr uint64_t no_execute = uint64_t{1} << 63;
constexpr uint64_t large_page_size = 2 * 1024 * 1024;
constexpr uint32_t pe_directory_base_reloc = 5;
constexpr uint32_t pe_reloc_dir64 = 10;
constexpr uint32_t section_execute = 0x20000000;
constexpr uint32_t section_write = 0x80000000;

uint64_t root_physical = 0;
uint64_t runtime_root_physical = 0;
uint64_t table_page_count = 0;
uint64_t small_page_count = 0;
uint64_t large_page_count = 0;
uint64_t high_image_physical = 0;
uint64_t high_image_size = 0;
uint64_t high_image_page_count = 0;
uint32_t high_relocation_rva = 0;
uint32_t high_relocation_size = 0;

SpinLock permission_lock;
volatile uint64_t shootdown_address = 0;
volatile uint64_t shootdown_pages = 1;
volatile uint64_t shootdown_sequence = 0;
volatile uint64_t shootdown_ack[smp::maximum_processors];
volatile uint64_t invalidation_count[smp::maximum_processors];
volatile uint64_t completed_shootdowns = 0;
volatile bool shootdown_fault = false;

void invalidate_page(uint64_t virtual_address);

void invalidate_page_range(uint64_t virtual_address, uint64_t pages) {
    for (uint64_t page = 0; page < pages; ++page) {
        invalidate_page(virtual_address + page * pmm::page_size);
    }
}

// permission_lock held. Broadcasts one shootdown sequence for [base, pages).
bool complete_shootdown_range(uint64_t virtual_address, uint64_t pages) {
    const uint32_t cpu_count = smp::online_processor_count();
    if (pages == 0 || cpu_count == 0 || cpu_count > smp::maximum_processors) {
        return false;
    }

    const uint64_t sequence =
        __atomic_load_n(&shootdown_sequence, __ATOMIC_RELAXED) + 1;
    __atomic_store_n(&shootdown_address, virtual_address, __ATOMIC_RELAXED);
    __atomic_store_n(&shootdown_pages, pages, __ATOMIC_RELAXED);
    __atomic_store_n(&shootdown_sequence, sequence, __ATOMIC_RELEASE);

    invalidate_page_range(virtual_address, pages);
    __atomic_add_fetch(&invalidation_count[0], 1, __ATOMIC_RELAXED);
    __atomic_store_n(&shootdown_ack[0], sequence, __ATOMIC_RELEASE);

    bool sent = true;
    for (uint32_t cpu_index = 1; cpu_index < cpu_count; ++cpu_index) {
        if (!smp::signal_processor(cpu_index, cpu::tlb_shootdown_vector)) {
            sent = false;
            break;
        }
    }
    bool acknowledged = sent;
    if (sent) {
        acknowledged = false;
        for (uint32_t guard = 0; guard < 100000000; ++guard) {
            bool complete = true;
            for (uint32_t cpu_index = 1; cpu_index < cpu_count; ++cpu_index) {
                if (__atomic_load_n(&shootdown_ack[cpu_index],
                                    __ATOMIC_ACQUIRE) != sequence) {
                    complete = false;
                    break;
                }
            }
            if (complete) {
                acknowledged = true;
                break;
            }
            cpu::relax();
        }
    }

    if (!acknowledged) {
        __atomic_store_n(&shootdown_fault, true, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_add_fetch(&completed_shootdowns, 1, __ATOMIC_RELEASE);
    return true;
}

struct PeLayout {
    uint32_t image_size;
    uint32_t section_table_offset;
    uint16_t section_count;
    uint32_t relocation_rva;
    uint32_t relocation_size;
};

uint16_t read16(const void* pointer) {
    const auto* bytes = static_cast<const uint8_t*>(pointer);
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t read32(const void* pointer) {
    const auto* bytes = static_cast<const uint8_t*>(pointer);
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read64(const void* pointer) {
    const auto* bytes = static_cast<const uint8_t*>(pointer);
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

void write64(void* pointer, uint64_t value) {
    auto* bytes = static_cast<uint8_t*>(pointer);
    for (size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

uint64_t read_cr3() {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

uint64_t read_cr4() {
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

void cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(0));
}

uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(static_cast<uint32_t>(value)),
                       "d"(static_cast<uint32_t>(value >> 32))
                     : "memory");
}

bool enable_nx() {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    cpuid(0x80000000, a, b, c, d);
    if (a < 0x80000001) {
        return false;
    }
    cpuid(0x80000001, a, b, c, d);
    if ((d & (uint32_t{1} << 20)) == 0) {
        return false;
    }

    constexpr uint32_t efer = 0xc0000080;
    constexpr uint64_t nxe = uint64_t{1} << 11;
    const uint64_t value = read_msr(efer);
    if ((value & nxe) == 0) {
        write_msr(efer, value | nxe);
    }
    return true;
}

uint64_t* table_at(uint64_t physical_address) {
    return static_cast<uint64_t*>(pmm::physical_to_virtual(physical_address));
}

uint64_t pml4_index(uint64_t virtual_address) {
    return (virtual_address >> 39) & 0x1ff;
}

uint64_t pdpt_index(uint64_t virtual_address) {
    return (virtual_address >> 30) & 0x1ff;
}

uint64_t pd_index(uint64_t virtual_address) {
    return (virtual_address >> 21) & 0x1ff;
}

uint64_t pt_index(uint64_t virtual_address) {
    return (virtual_address >> 12) & 0x1ff;
}

uint64_t next_table(uint64_t& entry) {
    if ((entry & present) != 0) {
        if ((entry & huge_page) != 0) {
            return 0;
        }
        return entry & address_mask;
    }

    const uint64_t physical = pmm::allocate_pages(1);
    if (physical == 0) {
        return 0;
    }
    entry = physical | present | writable;
    ++table_page_count;
    return physical;
}

bool map_2m(uint64_t physical, uint64_t virtual_address, uint64_t flags) {
    if ((physical & (large_page_size - 1)) != 0 ||
        (virtual_address & (large_page_size - 1)) != 0) {
        return false;
    }

    uint64_t* root = table_at(root_physical);
    const uint64_t pdpt_physical = next_table(root[pml4_index(virtual_address)]);
    if (pdpt_physical == 0) {
        return false;
    }
    uint64_t* pdpt = table_at(pdpt_physical);
    const uint64_t pd_physical = next_table(pdpt[pdpt_index(virtual_address)]);
    if (pd_physical == 0) {
        return false;
    }

    uint64_t* pd = table_at(pd_physical);
    uint64_t& entry = pd[pd_index(virtual_address)];
    if ((entry & present) != 0) {
        return false;
    }
    entry = physical | present | huge_page | flags;
    ++large_page_count;
    return true;
}

bool map_4k(uint64_t physical, uint64_t virtual_address, uint64_t flags) {
    uint64_t* root = table_at(root_physical);
    const uint64_t pdpt_physical = next_table(root[pml4_index(virtual_address)]);
    if (pdpt_physical == 0) {
        return false;
    }
    uint64_t* pdpt = table_at(pdpt_physical);
    const uint64_t pd_physical = next_table(pdpt[pdpt_index(virtual_address)]);
    if (pd_physical == 0) {
        return false;
    }
    uint64_t* pd = table_at(pd_physical);
    const uint64_t pt_physical = next_table(pd[pd_index(virtual_address)]);
    if (pt_physical == 0) {
        return false;
    }

    uint64_t* pt = table_at(pt_physical);
    uint64_t& entry = pt[pt_index(virtual_address)];
    if ((entry & present) != 0) {
        return false;
    }
    entry = physical | present | flags;
    ++small_page_count;
    return true;
}

bool map_region(const pmm::Region& region) {
    uint64_t physical = region.base;
    while (physical < region.end) {
        const uint64_t virtual_address = direct_map_base + physical;
        if ((physical & (large_page_size - 1)) == 0 &&
            region.end - physical >= large_page_size) {
            if (!map_2m(physical, virtual_address, writable | no_execute)) {
                return false;
            }
            physical += large_page_size;
        } else {
            if (!map_4k(physical, virtual_address, writable | no_execute)) {
                return false;
            }
            physical += pmm::page_size;
        }
    }
    return true;
}

bool parse_pe(uint64_t loaded_base, uint64_t loaded_size, PeLayout& layout) {
    if (loaded_base == 0 || loaded_size < 4096) {
        return false;
    }
    const auto* image = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(loaded_base));
    if (read16(image) != 0x5a4d) {
        log::line("[vmm] loaded image has no MZ header");
        return false;
    }

    const uint32_t pe_offset = read32(image + 0x3c);
    if (pe_offset > loaded_size - 24) {
        return false;
    }
    const uint8_t* pe = image + pe_offset;
    if (read32(pe) != 0x00004550) {
        log::line("[vmm] loaded image has no PE signature");
        return false;
    }

    const uint16_t section_count = read16(pe + 6);
    const uint16_t optional_size = read16(pe + 20);
    const uint8_t* optional = pe + 24;
    if (optional_size < 120 || pe_offset + 24 + optional_size > loaded_size ||
        read16(optional) != 0x20b) {
        return false;
    }
    const uint64_t header_image_base = read64(optional + 24);
    // OVMF rewrites this field to the actual load address. The preferred high
    // base is a link-time contract, not a required value in the loaded header.
    if ((header_image_base != kernel_base && header_image_base != loaded_base) ||
        read32(optional + 32) != pmm::page_size ||
        read32(optional + 108) <= pe_directory_base_reloc) {
        log::write("[vmm] loaded optional header mismatch, image base: ");
        log::hex(read64(optional + 24));
        log::write(", alignment: ");
        log::hex(read32(optional + 32));
        log::write(", directories: ");
        log::decimal(read32(optional + 108));
        log::write('\n');
        return false;
    }

    const uint32_t image_size = read32(optional + 56);
    const uint64_t section_table = pe_offset + 24 + optional_size;
    if (image_size == 0 || image_size > loaded_size || section_count == 0 ||
        section_table > loaded_size ||
        static_cast<uint64_t>(section_count) * 40 > loaded_size - section_table) {
        log::write("[vmm] loaded section layout mismatch, image size: ");
        log::hex(image_size);
        log::write(", sections: ");
        log::decimal(section_count);
        log::write('\n');
        return false;
    }

    const uint8_t* relocation = optional + 112 + pe_directory_base_reloc * 8;
    const uint32_t relocation_rva = read32(relocation);
    const uint32_t relocation_size = read32(relocation + 4);
    if (relocation_size < 8 ||
        static_cast<uint64_t>(relocation_rva) + relocation_size > image_size) {
        log::write("[vmm] loaded relocation directory mismatch, RVA: ");
        log::hex(relocation_rva);
        log::write(", size: ");
        log::hex(relocation_size);
        log::write('\n');
        return false;
    }

    layout.image_size = image_size;
    layout.section_table_offset = static_cast<uint32_t>(section_table);
    layout.section_count = section_count;
    layout.relocation_rva = relocation_rva;
    layout.relocation_size = relocation_size;
    return true;
}

uint64_t high_page_flags(uint64_t loaded_base, const PeLayout& layout,
                         uint32_t page_rva) {
    bool found = false;
    bool executable = false;
    bool can_write = false;
    const auto* image = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(loaded_base));

    for (uint32_t index = 0; index < layout.section_count; ++index) {
        const uint8_t* section = image + layout.section_table_offset + index * 40;
        const uint32_t virtual_size = read32(section + 8);
        const uint32_t virtual_address = read32(section + 12);
        const uint32_t raw_size = read32(section + 16);
        const uint32_t characteristics = read32(section + 36);
        const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        if (span == 0) {
            continue;
        }
        const uint64_t end = static_cast<uint64_t>(virtual_address) +
                             ((static_cast<uint64_t>(span) + 4095) & ~uint64_t{4095});
        if (page_rva >= virtual_address && page_rva < end) {
            found = true;
            executable = executable || (characteristics & section_execute) != 0;
            can_write = can_write || (characteristics & section_write) != 0;
        }
    }

    if (!found) {
        return no_execute;  // Headers and alignment gaps: read-only, NX.
    }
    return (can_write ? writable : 0) | (executable ? 0 : no_execute);
}

bool apply_high_relocations(uint64_t loaded_image_base) {
    auto* image = static_cast<uint8_t*>(pmm::physical_to_virtual(high_image_physical));
    const uint64_t delta = kernel_base - loaded_image_base;
    const uint64_t end = static_cast<uint64_t>(high_relocation_rva) +
                         high_relocation_size;
    uint64_t position = high_relocation_rva;

    while (position < end) {
        if (end - position < 8) {
            return false;
        }
        const uint32_t page_rva = read32(image + position);
        const uint32_t block_size = read32(image + position + 4);
        if (block_size < 8 || (block_size & 1) != 0 || block_size > end - position) {
            return false;
        }

        const uint32_t entries = (block_size - 8) / 2;
        for (uint32_t index = 0; index < entries; ++index) {
            const uint16_t encoded = read16(image + position + 8 + index * 2);
            const uint16_t type = encoded >> 12;
            const uint32_t target =
                page_rva + static_cast<uint32_t>(encoded & 0x0fff);
            if (type == 0) {
                continue;
            }
            if (type != pe_reloc_dir64 ||
                static_cast<uint64_t>(target) + 8 > high_image_size) {
                return false;
            }
            write64(image + target, read64(image + target) + delta);
        }
        position += block_size;
    }
    return position == end;
}

uint64_t* find_4k_entry(uint64_t virtual_address) {
    if (root_physical == 0) {
        return nullptr;
    }
    uint64_t* root = table_at(root_physical);
    const uint64_t pml4e = root[pml4_index(virtual_address)];
    if ((pml4e & present) == 0) {
        return nullptr;
    }
    uint64_t* pdpt = table_at(pml4e & address_mask);
    const uint64_t pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & present) == 0 || (pdpte & huge_page) != 0) {
        return nullptr;
    }
    uint64_t* pd = table_at(pdpte & address_mask);
    const uint64_t pde = pd[pd_index(virtual_address)];
    if ((pde & present) == 0 || (pde & huge_page) != 0) {
        return nullptr;
    }
    uint64_t* pt = table_at(pde & address_mask);
    uint64_t& entry = pt[pt_index(virtual_address)];
    return (entry & present) != 0 ? &entry : nullptr;
}

}  // namespace

bool initialize() {
    if ((read_cr4() & (uint64_t{1} << 12)) != 0 ||
        (read_cr4() & (uint64_t{1} << 17)) != 0) {
        log::line("[vmm] LA57/PCID active; this kernel requires four-level non-PCID boot");
        return false;
    }
    if (!enable_nx()) {
        log::line("[vmm] CPU does not expose NX");
        return false;
    }

    const uint64_t firmware_root = read_cr3() & address_mask;
    root_physical = pmm::allocate_pages(1);
    if (root_physical == 0) {
        return false;
    }
    runtime_root_physical = 0;
    high_image_physical = 0;
    high_image_size = 0;
    high_image_page_count = 0;
    table_page_count = 1;
    small_page_count = 0;
    large_page_count = 0;

    memcpy(table_at(root_physical), table_at(firmware_root), pmm::page_size);
    // All high-half lower tables must be kernel-owned. Reject a firmware high
    // mapping instead of modifying a shared lower-level table in place.
    for (size_t index = 256; index < 512; ++index) {
        if ((table_at(root_physical)[index] & present) != 0) {
            log::line("[vmm] firmware high-half mappings conflict with this milestone");
            return false;
        }
    }

    for (size_t index = 0; index < pmm::region_count(); ++index) {
        if (!map_region(pmm::region(index))) {
            log::line("[vmm] direct-map construction failed");
            return false;
        }
    }

    write_cr3(root_physical);
    pmm::set_direct_map_offset(direct_map_base);
    return true;
}

bool prepare_higher_half(uint64_t loaded_image_base,
                         uint64_t loaded_image_size,
                         uint64_t stack_physical) {
    PeLayout layout{};
    if (!parse_pe(loaded_image_base, loaded_image_size, layout)) {
        log::write("[vmm] PE parse failed, base: ");
        log::hex(loaded_image_base);
        log::write(", size: ");
        log::hex(loaded_image_size);
        log::write('\n');
        return false;
    }
    if ((stack_physical & (pmm::page_size - 1)) != 0) {
        log::line("[vmm] unaligned high-stack physical base");
        return false;
    }

    high_image_size = layout.image_size;
    high_image_page_count =
        (high_image_size + pmm::page_size - 1) / pmm::page_size;
    high_relocation_rva = layout.relocation_rva;
    high_relocation_size = layout.relocation_size;
    high_image_physical = pmm::allocate_pages(high_image_page_count);
    if (high_image_physical == 0) {
        log::line("[vmm] cannot allocate high-image pages");
        return false;
    }

    for (uint32_t page = 0; page < high_image_size; page += pmm::page_size) {
        const uint64_t flags = high_page_flags(loaded_image_base, layout, page);
        if (!map_4k(high_image_physical + page, kernel_base + page, flags)) {
            log::write("[vmm] high-image map failed at RVA: ");
            log::hex(page);
            log::write('\n');
            return false;
        }
    }

    for (uint64_t page = 0; page < kernel_stack_pages; ++page) {
        if (!map_4k(stack_physical + page * pmm::page_size,
                    higher_half_stack_base + page * pmm::page_size,
                    writable | no_execute)) {
            log::write("[vmm] high-stack map failed at page: ");
            log::decimal(page);
            log::write('\n');
            return false;
        }
    }
    // higher_half_stack_guard is deliberately never mapped.

    runtime_root_physical = pmm::allocate_pages(1);
    if (runtime_root_physical == 0) {
        return false;
    }
    ++table_page_count;
    memcpy(table_at(runtime_root_physical), table_at(root_physical), pmm::page_size);
    memset(table_at(runtime_root_physical), 0, 256 * sizeof(uint64_t));
    return true;
}

bool finalize_higher_half(uint64_t loaded_image_base) {
    if (high_image_physical == 0 || high_image_size == 0) {
        return false;
    }
    memcpy(pmm::physical_to_virtual(high_image_physical),
           reinterpret_cast<const void*>(static_cast<uintptr_t>(loaded_image_base)),
           static_cast<size_t>(high_image_size));
    return apply_high_relocations(loaded_image_base);
}

bool activate_runtime() {
    if (runtime_root_physical == 0) {
        return false;
    }
    const uint64_t transition_root = root_physical;
    write_cr3(runtime_root_physical);
    root_physical = runtime_root_physical;

    // Only the transition root page was ours. Firmware's old lower-level
    // tables remain reserved, but are no longer reachable from the active CR3.
    if (transition_root != 0 && transition_root != runtime_root_physical) {
        pmm::free_pages(transition_root, 1);
        --table_page_count;
    }
    return true;
}

MappingInfo inspect_mapping(uint64_t virtual_address) {
    if (root_physical == 0) {
        return MappingInfo{false, false, false};
    }
    bool can_write = true;
    bool can_execute = true;
    const uint64_t* root = table_at(root_physical);
    const uint64_t pml4e = root[pml4_index(virtual_address)];
    if ((pml4e & present) == 0) {
        return MappingInfo{false, false, false};
    }
    can_write = can_write && (pml4e & writable) != 0;
    can_execute = can_execute && (pml4e & no_execute) == 0;

    const uint64_t* pdpt = table_at(pml4e & address_mask);
    const uint64_t pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & present) == 0) {
        return MappingInfo{false, false, false};
    }
    can_write = can_write && (pdpte & writable) != 0;
    can_execute = can_execute && (pdpte & no_execute) == 0;
    if ((pdpte & huge_page) != 0) {
        return MappingInfo{true, can_write, can_execute};
    }

    const uint64_t* pd = table_at(pdpte & address_mask);
    const uint64_t pde = pd[pd_index(virtual_address)];
    if ((pde & present) == 0) {
        return MappingInfo{false, false, false};
    }
    can_write = can_write && (pde & writable) != 0;
    can_execute = can_execute && (pde & no_execute) == 0;
    if ((pde & huge_page) != 0) {
        return MappingInfo{true, can_write, can_execute};
    }

    const uint64_t* pt = table_at(pde & address_mask);
    const uint64_t pte = pt[pt_index(virtual_address)];
    if ((pte & present) == 0) {
        return MappingInfo{false, false, false};
    }
    can_write = can_write && (pte & writable) != 0;
    can_execute = can_execute && (pte & no_execute) == 0;
    return MappingInfo{true, can_write, can_execute};
}

bool is_mapped(uint64_t virtual_address) {
    return inspect_mapping(virtual_address).present;
}

bool set_page_writable(uint64_t virtual_address, bool writable_value) {
    if ((virtual_address & (pmm::page_size - 1)) != 0 ||
        virtual_address < direct_map_base || smp::current_cpu_index() != 0) {
        return false;
    }

    const uint64_t flags = permission_lock.lock_irqsave();
    uint64_t* const entry = find_4k_entry(virtual_address);
    if (entry == nullptr) {
        permission_lock.unlock_irqrestore(flags);
        return false;
    }
    if (writable_value) {
        *entry |= writable;
    } else {
        *entry &= ~writable;
    }

    const bool ok = complete_shootdown_range(virtual_address, 1);
    permission_lock.unlock_irqrestore(flags);
    return ok;
}

void on_tlb_shootdown() {
    const uint64_t sequence =
        __atomic_load_n(&shootdown_sequence, __ATOMIC_ACQUIRE);
    if (sequence == 0) {
        return;
    }
    const uint64_t address =
        __atomic_load_n(&shootdown_address, __ATOMIC_RELAXED);
    const uint64_t pages = __atomic_load_n(&shootdown_pages, __ATOMIC_RELAXED);
    invalidate_page_range(address, pages == 0 ? 1 : pages);
    const uint32_t cpu_index = smp::current_cpu_index();
    if (cpu_index < smp::maximum_processors) {
        __atomic_add_fetch(&invalidation_count[cpu_index], 1, __ATOMIC_RELAXED);
        __atomic_store_n(&shootdown_ack[cpu_index], sequence, __ATOMIC_RELEASE);
    }
}

uint64_t tlb_shootdown_count() {
    return __atomic_load_n(&completed_shootdowns, __ATOMIC_ACQUIRE);
}

uint64_t tlb_invalidations_for_cpu(uint32_t cpu_index) {
    return cpu_index < smp::maximum_processors
               ? __atomic_load_n(&invalidation_count[cpu_index],
                                 __ATOMIC_ACQUIRE)
               : 0;
}

bool tlb_shootdown_failed() {
    return __atomic_load_n(&shootdown_fault, __ATOMIC_ACQUIRE);
}

bool map_device_page(uint64_t physical_address, uint64_t virtual_address) {
    if (physical_address == 0 ||
        ((physical_address | virtual_address) & (pmm::page_size - 1)) != 0 ||
        virtual_address < direct_map_base) {
        return false;
    }
    return map_4k(physical_address, virtual_address,
                  writable | write_through | cache_disable | no_execute);
}

bool map_kernel_stack(uint64_t physical_address, uint64_t stack_base,
                      uint64_t pages) {
    if (physical_address == 0 || pages == 0 || stack_base < direct_map_base ||
        ((physical_address | stack_base) & (pmm::page_size - 1)) != 0 ||
        pages > (UINT64_MAX - stack_base) / pmm::page_size ||
        is_mapped(stack_base - pmm::page_size)) {
        return false;
    }
    for (uint64_t page = 0; page < pages; ++page) {
        if (!map_4k(physical_address + page * pmm::page_size,
                    stack_base + page * pmm::page_size,
                    writable | no_execute)) {
            return false;
        }
    }
    return !is_mapped(stack_base - pmm::page_size);
}

bool unmap_kernel_stack(uint64_t stack_base, uint64_t pages) {
    if (pages == 0 || stack_base < direct_map_base ||
        (stack_base & (pmm::page_size - 1)) != 0 ||
        pages > (UINT64_MAX - stack_base) / pmm::page_size ||
        smp::current_cpu_index() != 0) {
        return false;
    }

    const uint64_t flags = permission_lock.lock_irqsave();
    for (uint64_t page = 0; page < pages; ++page) {
        uint64_t* const entry =
            find_4k_entry(stack_base + page * pmm::page_size);
        if (entry == nullptr) {
            permission_lock.unlock_irqrestore(flags);
            return false;
        }
        *entry = 0;
        if (small_page_count > 0) {
            --small_page_count;
        }
    }
    const bool ok = complete_shootdown_range(stack_base, pages);
    permission_lock.unlock_irqrestore(flags);
    return ok && !is_mapped(stack_base) &&
           !is_mapped(stack_base - pmm::page_size);
}

bool map_smp_trampoline(uint64_t code_physical, uint64_t data_physical) {
    constexpr uint64_t sipi_limit = 0x100000;
    if (code_physical < pmm::page_size ||
        data_physical != code_physical + pmm::page_size ||
        code_physical > sipi_limit - 2 * pmm::page_size ||
        ((code_physical | data_physical) & (pmm::page_size - 1)) != 0) {
        return false;
    }
    if (!map_4k(code_physical, code_physical, 0)) {
        return false;
    }
    return map_4k(data_physical, data_physical, writable | no_execute);
}

uint64_t root_physical_address() {
    return root_physical;
}

uint64_t page_table_pages() {
    return table_page_count;
}

uint64_t mapped_4k_pages() {
    return small_page_count;
}

uint64_t mapped_2m_pages() {
    return large_page_count;
}

uint64_t high_image_pages() {
    return high_image_page_count;
}

}  // namespace cxc::vmm
