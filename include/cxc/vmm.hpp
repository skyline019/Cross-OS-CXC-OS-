#pragma once

#include <stdint.h>

namespace cxc::vmm {

constexpr uint64_t direct_map_base = 0xffff800000000000;
constexpr uint64_t kernel_base = 0xffffffff80000000;
constexpr uint64_t kernel_stack_pages = 16;
constexpr uint64_t higher_half_stack_base =
    kernel_base - kernel_stack_pages * 4096;
constexpr uint64_t higher_half_stack_guard = higher_half_stack_base - 4096;
constexpr uint64_t higher_half_stack_top = kernel_base;

// Clones the active firmware PML4 so the current image/stack remain mapped,
// then adds a new NX 2 MiB-page direct map and loads the kernel-owned root.
bool initialize();

// Allocates and maps a second kernel image and the high stack. No image bytes
// are copied until finalize, so the final copy captures the final PMM state.
bool prepare_higher_half(uint64_t loaded_image_base,
                         uint64_t loaded_image_size,
                         uint64_t stack_physical);
bool finalize_higher_half(uint64_t loaded_image_base);

// Called by high-half code after the high GDT/IDT are operational. The new
// root contains only the direct map, high kernel, and high stack.
struct MappingInfo {
    bool present;
    bool writable;
    bool executable;
};

bool activate_runtime();
MappingInfo inspect_mapping(uint64_t virtual_address);
bool is_mapped(uint64_t virtual_address);

// Runtime 4 KiB permission update on the shared kernel root. The BSP performs
// a local invlpg and vector-51 shootdown to every other online CPU, waiting for
// per-CPU acknowledgements. Huge-page mappings are deliberately rejected.
bool set_page_writable(uint64_t virtual_address, bool writable);
void on_tlb_shootdown();
uint64_t tlb_shootdown_count();
uint64_t tlb_invalidations_for_cpu(uint32_t cpu_index);
bool tlb_shootdown_failed();

// Post-relocation mappings for ring-0 runtime services. Device pages are NX
// and cache-disabled; stack pages are RW/NX and their preceding guard stays
// unmapped. These functions allocate page-table pages from the PMM.
bool map_device_page(uint64_t physical_address, uint64_t virtual_address);
bool map_kernel_stack(uint64_t physical_address, uint64_t stack_base,
                      uint64_t pages);
// BSP-only: clear PTEs for a previously mapped kernel stack and shoot down the
// VA range. Does not return pages to the PMM — the caller frees the physical
// base it already recorded.
bool unmap_kernel_stack(uint64_t stack_base, uint64_t pages);

// Identity-maps the two loader-owned SIPI pages in the runtime root. Code is
// RX and the following parameter page is RW/NX; neither enters the PMM.
bool map_smp_trampoline(uint64_t code_physical, uint64_t data_physical);

uint64_t root_physical_address();
uint64_t page_table_pages();
uint64_t mapped_4k_pages();
uint64_t mapped_2m_pages();
uint64_t high_image_pages();

}  // namespace cxc::vmm
