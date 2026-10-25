#include <cxc/heap.hpp>

#include <cxc/log.hpp>
#include <cxc/pmm.hpp>
#include <cxc/spinlock.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void*, int, size_t);

namespace cxc::heap {
namespace {

constexpr uint64_t allocated_magic = 0x435843414c4c4f43;  // "CXCALLOC"
constexpr uint64_t free_magic = 0x4358434652454521;      // "CXCFREE!"
constexpr size_t alignment = 16;
constexpr uint64_t minimum_expansion_pages = 4;

struct Header {
    uint64_t size;  // Payload bytes, including alignment padding.
    uint64_t magic;
    Header* next;   // Used only while the block is free.
    uint64_t reserved;
};

static_assert(sizeof(Header) == 32);

Header* free_list = nullptr;
uint64_t reserved_page_count = 0;
SpinLock heap_lock;

uint64_t address_of(const void* pointer) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

uint64_t block_end(const Header* block) {
    return address_of(block) + sizeof(Header) + block->size;
}

void insert_free(Header* block) {
    block->magic = free_magic;
    Header* previous = nullptr;
    Header* current = free_list;
    while (current != nullptr && current < block) {
        previous = current;
        current = current->next;
    }
    if (current == block) {
        log::panic("heap double free");
    }

    block->next = current;
    if (previous == nullptr) {
        free_list = block;
    } else {
        previous->next = block;
    }

    if (current != nullptr && block_end(block) == address_of(current)) {
        block->size += sizeof(Header) + current->size;
        block->next = current->next;
        current->magic = 0;
    }

    if (previous != nullptr && block_end(previous) == address_of(block)) {
        previous->size += sizeof(Header) + block->size;
        previous->next = block->next;
        block->magic = 0;
    }
}

bool expand(uint64_t payload_size) {
    if (payload_size > (~uint64_t{0}) - sizeof(Header) - pmm::page_size) {
        return false;
    }
    uint64_t pages =
        (payload_size + sizeof(Header) + pmm::page_size - 1) / pmm::page_size;
    if (pages < minimum_expansion_pages) {
        pages = minimum_expansion_pages;
    }

    const uint64_t physical = pmm::allocate_pages(pages);
    if (physical == 0) {
        return false;
    }

    auto* block = static_cast<Header*>(pmm::physical_to_virtual(physical));
    block->size = pages * pmm::page_size - sizeof(Header);
    block->next = nullptr;
    block->reserved = 0;
    reserved_page_count += pages;
    insert_free(block);
    return true;
}

}  // namespace

void* allocate(size_t requested_size) {
    if (requested_size == 0) {
        return nullptr;
    }
    if (requested_size > (~size_t{0}) - (alignment - 1)) {
        return nullptr;
    }
    const size_t size = (requested_size + alignment - 1) & ~(alignment - 1);
    const uint64_t flags = heap_lock.lock_irqsave();

    for (;;) {
        Header* previous = nullptr;
        Header* block = free_list;
        while (block != nullptr && block->size < size) {
            previous = block;
            block = block->next;
        }

        if (block != nullptr) {
            if (previous == nullptr) {
                free_list = block->next;
            } else {
                previous->next = block->next;
            }

            if (block->size >= size + sizeof(Header) + alignment) {
                const uint64_t original_size = block->size;
                block->size = size;
                auto* remainder = reinterpret_cast<Header*>(
                    reinterpret_cast<uint8_t*>(block) + sizeof(Header) + size);
                remainder->size = original_size - size - sizeof(Header);
                remainder->next = nullptr;
                remainder->reserved = 0;
                insert_free(remainder);
            }

            block->magic = allocated_magic;
            block->next = nullptr;
            void* const result = reinterpret_cast<uint8_t*>(block) + sizeof(Header);
            heap_lock.unlock_irqrestore(flags);
            return result;
        }

        if (!expand(size)) {
            heap_lock.unlock_irqrestore(flags);
            return nullptr;
        }
    }
}

void* allocate_zero(size_t size) {
    void* pointer = allocate(size);
    if (pointer != nullptr) {
        memset(pointer, 0, size);
    }
    return pointer;
}

void* allocate_array(size_t count, size_t size) {
    if (count != 0 && size > (~size_t{0}) / count) {
        return nullptr;
    }
    return allocate_zero(count * size);
}

void release(void* pointer) {
    if (pointer == nullptr) {
        return;
    }
    if ((address_of(pointer) & (alignment - 1)) != 0) {
        log::panic("unaligned heap release");
    }

    const uint64_t flags = heap_lock.lock_irqsave();
    auto* block = reinterpret_cast<Header*>(
        static_cast<uint8_t*>(pointer) - sizeof(Header));
    if (block->magic != allocated_magic) {
        log::panic("invalid or double heap release");
    }
    insert_free(block);
    heap_lock.unlock_irqrestore(flags);
}

uint64_t reserved_pages() {
    const uint64_t flags = heap_lock.lock_irqsave();
    const uint64_t result = reserved_page_count;
    heap_lock.unlock_irqrestore(flags);
    return result;
}

}  // namespace cxc::heap
