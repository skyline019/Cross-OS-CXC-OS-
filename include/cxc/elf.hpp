#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::elf {

struct LoadResult {
    uint64_t entry;
    uint64_t at_phdr;
    uint64_t at_phent;
    uint64_t at_phnum;
    uint32_t segments;
    uint32_t pages;
    uint32_t failure;
};

// BSP-only: map PT_LOAD segments of a static ET_EXEC ELF64 into `user_root`.
// Rejects dynamic/interpreter images and non-x86_64. Does not create a stack.
bool load_static_et_exec(uint64_t user_root_physical, const uint8_t* image,
                         size_t image_size, LoadResult& result);

}  // namespace cxc::elf
