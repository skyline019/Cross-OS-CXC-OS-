#include <cxc/pmm.hpp>

#include <cxc/log.hpp>
#include <cxc/spinlock.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void*, int, size_t);

namespace cxc::pmm {
namespace {

constexpr uint64_t minimum_physical_address = 0x200000;
// Reserve the top 1 TiB of virtual space for the higher-half kernel and
// future per-CPU/user mappings instead of letting the direct map cover it.
constexpr uint64_t maximum_direct_physical_address = 0x00007f0000000000;
constexpr size_t maximum_regions = 256;

// Stored inside a free extent itself. Both links and extents use physical
// addresses, so changing from identity mapping to the direct map requires no
// list relocation or per-node fixup.
struct Extent {
    uint64_t next;
    uint64_t pages;
};

uint64_t free_head = 0;
uint64_t direct_map_offset = 0;
uint64_t total_page_count = 0;
uint64_t free_page_count_value = 0;
Region regions[maximum_regions];
size_t region_count_value = 0;
SpinLock allocator_lock;

Extent* extent_at(uint64_t physical_address) {
    return static_cast<Extent*>(physical_to_virtual(physical_address));
}

uint64_t extent_end(uint64_t base, uint64_t pages) {
    return base + pages * page_size;
}

bool add_region(uint64_t base, uint64_t pages, size_t& count) {
    constexpr uint64_t maximum = ~uint64_t{0};
    if (pages > (maximum - base) / page_size) {
        return true;
    }

    uint64_t end = base + pages * page_size;
    if (end <= minimum_physical_address ||
        base >= maximum_direct_physical_address) {
        return true;
    }
    if (base < minimum_physical_address) {
        base = minimum_physical_address;
    }
    if (end > maximum_direct_physical_address) {
        end = maximum_direct_physical_address;
    }
    if ((base & (page_size - 1)) != 0) {
        base = (base + page_size - 1) & ~(page_size - 1);
    }
    end &= ~(page_size - 1);
    if (end <= base) {
        return true;
    }
    if (count == maximum_regions) {
        return false;
    }

    regions[count++] = Region{base, end};
    return true;
}

void sort_and_merge_regions() {
    for (size_t index = 1; index < region_count_value; ++index) {
        const Region value = regions[index];
        size_t position = index;
        while (position != 0 && regions[position - 1].base > value.base) {
            regions[position] = regions[position - 1];
            --position;
        }
        regions[position] = value;
    }

    size_t output = 0;
    for (size_t index = 0; index < region_count_value; ++index) {
        if (output != 0 && regions[index].base <= regions[output - 1].end) {
            if (regions[index].end > regions[output - 1].end) {
                regions[output - 1].end = regions[index].end;
            }
        } else {
            regions[output++] = regions[index];
        }
    }
    region_count_value = output;
}

}  // namespace

bool initialize(const uefi::BootMemoryMap& map) {
    allocator_lock.unlock();
    if (map.descriptors == nullptr ||
        map.descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return false;
    }

    free_head = 0;
    direct_map_offset = 0;
    total_page_count = 0;
    free_page_count_value = 0;
    region_count_value = 0;

    for (uint64_t offset = 0;
         offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= map.size;) {
        const auto* descriptor = reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(
            reinterpret_cast<const uint8_t*>(map.descriptors) + offset);
        if (descriptor->type == EfiConventionalMemory &&
            !add_region(descriptor->physical_start, descriptor->number_of_pages,
                        region_count_value)) {
            return false;
        }
        if (offset > ~uint64_t{0} - map.descriptor_size) {
            return false;
        }
        offset += map.descriptor_size;
    }

    sort_and_merge_regions();

    // One metadata write per free extent, rather than one per free page.
    // Ascending regions are pushed to form a descending-address list.
    for (size_t index = 0; index < region_count_value; ++index) {
        const uint64_t pages = (regions[index].end - regions[index].base) / page_size;
        Extent* extent = extent_at(regions[index].base);
        extent->next = free_head;
        extent->pages = pages;
        free_head = regions[index].base;
        total_page_count += pages;
    }
    free_page_count_value = total_page_count;
    return total_page_count != 0;
}

void set_direct_map_offset(uint64_t offset) {
    direct_map_offset = offset;
}

void* physical_to_virtual(uint64_t physical_address) {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(physical_address + direct_map_offset));
}

uint64_t allocate_pages(uint64_t count) {
    const uint64_t flags = allocator_lock.lock_irqsave();
    if (count == 0 || count > free_page_count_value ||
        count > (~uint64_t{0}) / page_size) {
        allocator_lock.unlock_irqrestore(flags);
        return 0;
    }

    uint64_t* link = &free_head;
    while (*link != 0) {
        const uint64_t base = *link;
        Extent* extent = extent_at(base);
        if (extent->pages >= count) {
            // Allocate from the high end. A partial allocation leaves the
            // metadata node at the same base, avoiding any node movement.
            const uint64_t result = base + (extent->pages - count) * page_size;
            extent->pages -= count;
            if (extent->pages == 0) {
                *link = extent->next;
            }
            free_page_count_value -= count;
            memset(physical_to_virtual(result), 0,
                   static_cast<size_t>(count * page_size));
            allocator_lock.unlock_irqrestore(flags);
            return result;
        }
        link = &extent->next;
    }
    allocator_lock.unlock_irqrestore(flags);
    return 0;
}

uint64_t allocate_pages_below(uint64_t count, uint64_t limit) {
    const uint64_t flags = allocator_lock.lock_irqsave();
    limit &= ~(page_size - 1);
    if (count == 0 || count > free_page_count_value ||
        count > (~uint64_t{0}) / page_size ||
        limit <= minimum_physical_address) {
        allocator_lock.unlock_irqrestore(flags);
        return 0;
    }

    uint64_t* link = &free_head;
    while (*link != 0) {
        const uint64_t base = *link;
        Extent* extent = extent_at(base);
        if (base >= limit) {
            link = &extent->next;
            continue;
        }

        const uint64_t end = extent_end(base, extent->pages);
        const uint64_t usable_end = end < limit ? end : limit;
        const uint64_t usable_pages = (usable_end - base) / page_size;
        if (usable_pages >= count) {
            const uint64_t result = usable_end - count * page_size;
            const uint64_t next = extent->next;
            const uint64_t low_pages = usable_pages - count;
            const uint64_t high_pages = (end - usable_end) / page_size;

            // If the limit splits an extent, retain the upper tail as a new
            // higher-address node. The list remains sorted in descending order.
            if (high_pages != 0) {
                Extent* high = extent_at(usable_end);
                high->pages = high_pages;
                high->next = low_pages == 0 ? next : base;
                *link = usable_end;
            } else if (low_pages == 0) {
                *link = next;
            }

            if (low_pages != 0) {
                extent->pages = low_pages;
                extent->next = next;
            }
            free_page_count_value -= count;
            memset(physical_to_virtual(result), 0,
                   static_cast<size_t>(count * page_size));
            allocator_lock.unlock_irqrestore(flags);
            return result;
        }
        link = &extent->next;
    }
    allocator_lock.unlock_irqrestore(flags);
    return 0;
}

void free_pages(uint64_t physical_address, uint64_t count) {
    if (physical_address < minimum_physical_address ||
        (physical_address & (page_size - 1)) != 0 || count == 0 ||
        count > ((~uint64_t{0}) - physical_address) / page_size) {
        log::panic("invalid physical page release");
    }
    const uint64_t flags = allocator_lock.lock_irqsave();
    const uint64_t released_end = extent_end(physical_address, count);

    uint64_t* link = &free_head;
    uint64_t* higher_link = nullptr;
    uint64_t higher_base = 0;
    while (*link != 0 && *link > physical_address) {
        const uint64_t current_base = *link;
        Extent* current = extent_at(current_base);
        const uint64_t current_end = extent_end(current_base, current->pages);
        if (physical_address < current_end && current_base < released_end) {
            log::panic("physical extent overlap or double free");
        }
        higher_link = link;
        higher_base = current_base;
        link = &current->next;
    }
    if (*link != 0) {
        Extent* next = extent_at(*link);
        const uint64_t next_end = extent_end(*link, next->pages);
        if (physical_address < next_end && *link < released_end) {
            log::panic("physical extent overlap or double free");
        }
    }

    Extent* released = extent_at(physical_address);
    released->pages = count;
    released->next = *link;
    *link = physical_address;

    // Merge with the lower neighbor first. Its next link already points past
    // the released extent, so only the page count and incoming link change.
    if (released->next != 0) {
        const uint64_t lower_base = released->next;
        Extent* lower = extent_at(lower_base);
        if (extent_end(lower_base, lower->pages) == physical_address) {
            lower->pages += released->pages;
            *link = lower_base;
            physical_address = lower_base;
            released = lower;
        }
    }

    // higher_link refers to the link that points at the higher neighbor, not
    // to higher->next. This lets us unlink the higher node without rescanning.
    if (higher_link != nullptr &&
        extent_end(physical_address, released->pages) == higher_base) {
        Extent* higher = extent_at(higher_base);
        released->pages += higher->pages;
        *higher_link = physical_address;
    }

    free_page_count_value += count;
    if (free_page_count_value > total_page_count) {
        log::panic("physical allocator accounting overflow");
    }
    allocator_lock.unlock_irqrestore(flags);
}

uint64_t total_pages() {
    return total_page_count;
}

uint64_t free_pages_count() {
    const uint64_t flags = allocator_lock.lock_irqsave();
    const uint64_t result = free_page_count_value;
    allocator_lock.unlock_irqrestore(flags);
    return result;
}

uint64_t extent_count() {
    const uint64_t flags = allocator_lock.lock_irqsave();
    uint64_t count = 0;
    uint64_t current = free_head;
    while (current != 0) {
        ++count;
        current = extent_at(current)->next;
    }
    allocator_lock.unlock_irqrestore(flags);
    return count;
}

size_t region_count() {
    return region_count_value;
}

const Region& region(size_t index) {
    if (index >= region_count_value) {
        log::panic("physical memory region index out of range");
    }
    return regions[index];
}

}  // namespace cxc::pmm
