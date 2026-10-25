#include <cxc/self_test.hpp>

#include <cxc/cpu.hpp>
#include <cxc/heap.hpp>
#include <cxc/log.hpp>
#include <cxc/pmm.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::self_test {
namespace {

constexpr size_t benchmark_batch = 512;
uint64_t benchmark_pages[benchmark_batch];
uint64_t fragmented_pages[8];

struct PmmSlot {
    uint64_t base;
    uint64_t pages;
};

struct HeapSlot {
    uint8_t* pointer;
    size_t size;
    uint8_t pattern;
};

PmmSlot pmm_slots[48];
HeapSlot heap_slots[32];
uint8_t memory_destination[160];
uint8_t memory_source[128];
uint64_t random_state = 0x9e3779b97f4a7c15;

uint64_t next_random() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

void require(bool condition, const char* message) {
    if (!condition) {
        log::panic(message);
    }
}

void test_memory_routines() {
    for (size_t offset = 0; offset < 9; ++offset) {
        for (size_t length = 0; length < 66; ++length) {
            memset(memory_destination, 0xa5, sizeof(memory_destination));
            memset(memory_destination + offset, 0x5a, length);
            for (size_t index = 0; index < sizeof(memory_destination); ++index) {
                const bool inside = index >= offset && index < offset + length;
                require(memory_destination[index] == (inside ? 0x5a : 0xa5),
                        "optimized memset boundary failure");
            }
        }
    }

    for (size_t source_offset = 0; source_offset < 8; ++source_offset) {
        for (size_t destination_offset = 0; destination_offset < 8;
             ++destination_offset) {
            for (size_t length = 0; length < 34; ++length) {
                for (size_t index = 0; index < sizeof(memory_source); ++index) {
                    memory_source[index] = static_cast<uint8_t>(index * 13 + 7);
                }
                memset(memory_destination, 0xcd, sizeof(memory_destination));
                memcpy(memory_destination + destination_offset,
                       memory_source + source_offset, length);
                for (size_t index = 0; index < sizeof(memory_destination); ++index) {
                    const bool inside = index >= destination_offset &&
                                        index < destination_offset + length;
                    const uint8_t expected = inside
                        ? memory_source[source_offset + index - destination_offset]
                        : static_cast<uint8_t>(0xcd);
                    require(memory_destination[index] == expected,
                            "optimized memcpy boundary failure");
                }
            }
        }
    }
    log::line("[ok] aligned/unaligned memory routine boundary tests");
}

void test_direct_map() {
    const uint64_t page = pmm::allocate_pages(1);
    require(page != 0, "direct-map test allocation failed");
    auto* memory = static_cast<uint8_t*>(pmm::physical_to_virtual(page));
    require(reinterpret_cast<uintptr_t>(memory) == vmm::direct_map_base + page,
            "direct-map address calculation is wrong");
    memory[0] = 0x5a;
    memory[pmm::page_size - 1] = 0xa5;
    require(memory[0] == 0x5a && memory[pmm::page_size - 1] == 0xa5,
            "direct-map read/write failed");
    pmm::free_pages(page, 1);
    log::line("[ok] direct-map read/write and PMM virtual access");
}

void test_pmm() {
    const uint64_t before = pmm::free_pages_count();
    const uint64_t extents_before = pmm::extent_count();
    const uint64_t one = pmm::allocate_pages(1);
    const uint64_t three = pmm::allocate_pages(3);
    require(one != 0 && three != 0, "PMM test allocation failed");
    require((one & (pmm::page_size - 1)) == 0 &&
                (three & (pmm::page_size - 1)) == 0,
            "PMM returned an unaligned page");
    require(before >= 4 && pmm::free_pages_count() == before - 4,
            "PMM accounting mismatch");

    auto* first = static_cast<uint8_t*>(pmm::physical_to_virtual(one));
    auto* run = static_cast<uint8_t*>(pmm::physical_to_virtual(three));
    first[0] = 0xa5;
    run[0] = 0x11;
    run[pmm::page_size] = 0x22;
    run[2 * pmm::page_size] = 0x33;
    require(first[0] == 0xa5 && run[0] == 0x11 &&
                run[pmm::page_size] == 0x22 &&
                run[2 * pmm::page_size] == 0x33,
            "PMM memory read/write failed");

    pmm::free_pages(one, 1);
    pmm::free_pages(three, 3);
    require(pmm::free_pages_count() == before &&
                pmm::extent_count() == extents_before,
            "PMM free accounting or coalescing mismatch");
    log::line("[ok] PMM single-page and contiguous three-page tests");
}

void test_fragmentation_and_coalescing() {
    const uint64_t free_before = pmm::free_pages_count();
    const uint64_t extents_before = pmm::extent_count();

    // Reserve a known contiguous arena, then constrain every test allocation
    // below its end. Without this bound, a firmware-dependent memory map can
    // interleave allocations across unrelated extents and invalidate the test.
    const uint64_t arena = pmm::allocate_pages(16);
    require(arena != 0, "fragmentation test arena allocation failed");
    const uint64_t arena_end = arena + 16 * pmm::page_size;
    pmm::free_pages(arena, 16);
    require(pmm::free_pages_count() == free_before &&
                pmm::extent_count() == extents_before,
            "fragmentation arena did not restore allocator state");

    for (size_t index = 0; index < 8; ++index) {
        fragmented_pages[index] = pmm::allocate_pages_below(1, arena_end);
        require(fragmented_pages[index] != 0,
                "fragmentation test allocation failed");
        require(fragmented_pages[index] >= arena &&
                    fragmented_pages[index] < arena_end,
                "fragmentation test escaped its bounded arena");
        if (index != 0) {
            require(fragmented_pages[index] + pmm::page_size ==
                        fragmented_pages[index - 1],
                    "fragmentation test pages are not contiguous");
        }
    }
    for (size_t index = 0; index < 8; index += 2) {
        pmm::free_pages(fragmented_pages[index], 1);
    }
    require(pmm::extent_count() > extents_before,
            "fragmentation test did not create separate extents");
    for (size_t index = 1; index < 8; index += 2) {
        pmm::free_pages(fragmented_pages[index], 1);
    }
    require(pmm::free_pages_count() == free_before &&
                pmm::extent_count() == extents_before,
            "fragmented extents did not fully coalesce");

    const uint64_t eight = pmm::allocate_pages_below(8, arena_end);
    require(eight == arena_end - 8 * pmm::page_size,
            "coalesced eight-page allocation failed");
    pmm::free_pages(eight, 8);
    require(pmm::free_pages_count() == free_before &&
                pmm::extent_count() == extents_before,
            "eight-page run did not restore allocator state");
    log::line("[ok] odd/even fragmentation, full coalescing, contiguous 8 pages");
}

void test_pmm_stress() {
    const uint64_t free_before = pmm::free_pages_count();
    const uint64_t extents_before = pmm::extent_count();
    uint64_t used_pages = 0;
    uint64_t failed_allocations = 0;
    random_state = 0xd1b54a32d192ed03;

    for (size_t step = 0; step < 2000; ++step) {
        const size_t index = next_random() % 48;
        PmmSlot& slot = pmm_slots[index];
        if (slot.base != 0) {
            auto* memory = static_cast<uint8_t*>(pmm::physical_to_virtual(slot.base));
            const uint8_t expected = static_cast<uint8_t>(index + 1);
            require(memory[0] == expected &&
                        memory[slot.pages * pmm::page_size - 1] == expected,
                    "PMM stress detected overlapping or corrupted pages");
            pmm::free_pages(slot.base, slot.pages);
            used_pages -= slot.pages;
            slot = PmmSlot{};
        } else {
            const uint64_t pages = (next_random() % 8) + 1;
            const uint64_t base = pmm::allocate_pages(pages);
            if (base == 0) {
                ++failed_allocations;
            } else {
                slot = PmmSlot{base, pages};
                used_pages += pages;
                auto* memory = static_cast<uint8_t*>(pmm::physical_to_virtual(base));
                memory[0] = static_cast<uint8_t>(index + 1);
                memory[pages * pmm::page_size - 1] =
                    static_cast<uint8_t>(index + 1);
            }
        }
        require(pmm::free_pages_count() == free_before - used_pages,
                "PMM stress accounting mismatch");
    }

    for (size_t index = 0; index < 48; ++index) {
        if (pmm_slots[index].base != 0) {
            pmm::free_pages(pmm_slots[index].base, pmm_slots[index].pages);
            pmm_slots[index] = PmmSlot{};
        }
    }
    require(pmm::free_pages_count() == free_before &&
                pmm::extent_count() == extents_before,
            "PMM stress did not restore the original extent list");
    log::write("[ok] PMM deterministic 2000-step stress; failed allocations: ");
    log::decimal(failed_allocations);
    log::write('\n');
}

void test_heap() {
    auto* a = static_cast<uint8_t*>(heap::allocate(24));
    auto* b = static_cast<uint8_t*>(heap::allocate(100));
    auto* large = static_cast<uint8_t*>(heap::allocate(6000));
    require(a != nullptr && b != nullptr && large != nullptr,
            "heap allocation failed");
    require((reinterpret_cast<uintptr_t>(a) & 15) == 0 &&
                (reinterpret_cast<uintptr_t>(large) & 15) == 0,
            "heap alignment failed");

    for (size_t index = 0; index < 24; ++index) {
        a[index] = static_cast<uint8_t>(index);
    }
    for (size_t index = 0; index < 6000; ++index) {
        large[index] = static_cast<uint8_t>(index ^ 0x5a);
    }

    heap::release(b);
    void* reused = heap::allocate(64);
    require(reused == b, "heap did not reuse an isolated free block");
    heap::release(reused);

    auto* zeros = static_cast<uint8_t*>(heap::allocate_array(32, 4));
    require(zeros != nullptr, "heap array allocation failed");
    for (size_t index = 0; index < 128; ++index) {
        require(zeros[index] == 0, "heap zero allocation was not zeroed");
    }

    for (size_t index = 0; index < 24; ++index) {
        require(a[index] == static_cast<uint8_t>(index),
                "heap neighbor corruption detected");
    }
    for (size_t index = 0; index < 6000; ++index) {
        require(large[index] == static_cast<uint8_t>(index ^ 0x5a),
                "large heap allocation corruption detected");
    }

    heap::release(zeros);
    heap::release(large);
    heap::release(a);

    log::write("[ok] heap split/coalesce/zero tests; reserved pages: ");
    log::decimal(heap::reserved_pages());
    log::write('\n');
}

void test_heap_stress() {
    random_state = 0x94d049bb133111eb;
    uint64_t failed_allocations = 0;
    for (size_t step = 0; step < 1000; ++step) {
        const size_t index = next_random() % 32;
        HeapSlot& slot = heap_slots[index];
        if (slot.pointer != nullptr) {
            for (size_t byte = 0; byte < slot.size; ++byte) {
                require(slot.pointer[byte] == slot.pattern,
                        "heap stress detected block corruption");
            }
            heap::release(slot.pointer);
            slot = HeapSlot{};
        } else {
            const size_t size = static_cast<size_t>(next_random() % 2048) + 1;
            auto* pointer = static_cast<uint8_t*>(heap::allocate(size));
            if (pointer == nullptr) {
                ++failed_allocations;
            } else {
                const uint8_t pattern =
                    static_cast<uint8_t>((step * 37 + index * 11 + 1) & 0xff);
                slot = HeapSlot{pointer, size, pattern};
                for (size_t byte = 0; byte < size; ++byte) {
                    pointer[byte] = pattern;
                }
            }
        }
    }

    for (size_t index = 0; index < 32; ++index) {
        if (heap_slots[index].pointer != nullptr) {
            heap::release(heap_slots[index].pointer);
            heap_slots[index] = HeapSlot{};
        }
    }
    log::write("[ok] heap deterministic 1000-step stress; failed allocations: ");
    log::decimal(failed_allocations);
    log::write('\n');
}

void benchmark_pmm() {
    const uint64_t free_before = pmm::free_pages_count();
    const uint64_t extents_before = pmm::extent_count();

    const uint64_t allocate_start = cpu::read_tsc();
    for (size_t index = 0; index < benchmark_batch; ++index) {
        benchmark_pages[index] = pmm::allocate_pages(1);
        require(benchmark_pages[index] != 0, "PMM benchmark allocation failed");
    }
    const uint64_t allocate_end = cpu::read_tsc();

    // Reverse order is the allocator's ordered-release best case. This is a
    // regression benchmark, not a claim about arbitrary fragmentation loads.
    const uint64_t free_start = cpu::read_tsc();
    for (size_t index = benchmark_batch; index != 0; --index) {
        pmm::free_pages(benchmark_pages[index - 1], 1);
    }
    const uint64_t free_end = cpu::read_tsc();

    require(pmm::free_pages_count() == free_before &&
                pmm::extent_count() == extents_before,
            "PMM benchmark did not restore allocator state");

    const uint64_t allocate_cycles = allocate_end - allocate_start;
    const uint64_t free_cycles = free_end - free_start;
    log::write("[bench] inline-extent 512 alloc cycles: ");
    log::decimal(allocate_cycles);
    log::write(", per page: ");
    log::decimal(allocate_cycles / benchmark_batch);
    log::write('\n');
    log::write("[bench] 512 ordered frees cycles: ");
    log::decimal(free_cycles);
    log::write(", per page: ");
    log::decimal(free_cycles / benchmark_batch);
    log::write('\n');
}

}  // namespace

void run() {
    test_memory_routines();
    test_direct_map();
    test_pmm();
    test_fragmentation_and_coalescing();
    test_pmm_stress();
    test_heap();
    test_heap_stress();
    benchmark_pmm();
}

}  // namespace cxc::self_test
