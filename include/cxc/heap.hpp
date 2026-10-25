#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::heap {

void* allocate(size_t size);
void* allocate_zero(size_t size);
void* allocate_array(size_t count, size_t size);
void release(void* pointer);

uint64_t reserved_pages();

}  // namespace cxc::heap
