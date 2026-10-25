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

// M35 ADR / M36 probe: first user VA window (canonical low half).
constexpr uint64_t user_probe_base = 0x0000000000400000;
// map_user_page flags (OR into the final PTE; Present is always added).
constexpr uint64_t user_map_writable = uint64_t{1} << 0;
constexpr uint64_t user_map_executable = uint64_t{1} << 1;

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
    bool user;
};

// M36: BSP-only user address space (ADR D1–D5). Shares kernel high-half PML4
// entries; low half starts empty. Does not introduce ring-3 or TSS.
uint64_t kernel_address_space();
uint64_t create_user_address_space();
bool destroy_user_address_space(uint64_t user_root_physical);
// M83: deep-copy low-half user pages into a new address space (fork).
uint64_t clone_user_address_space(uint64_t src_root_physical);
bool map_user_page(uint64_t user_root_physical, uint64_t virtual_address,
                   uint64_t physical_address, uint64_t map_flags);
// BSP-only: clear one user PTE, free its frame, and invlpg (M43 munmap).
bool unmap_user_page(uint64_t user_root_physical, uint64_t virtual_address);
bool activate_address_space(uint64_t root_physical);
// Walks `root` (not necessarily the active CR3) for evidence checks.
MappingInfo inspect_mapping_in(uint64_t root_physical,
                               uint64_t virtual_address);

struct UserAsProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t cr3_switch;
    uint32_t write_ok;
    uint32_t read_ok;
    uint32_t user_bit;
    uint64_t user_root;
    uint64_t probe_physical;
};

bool run_user_as_probe(UserAsProbeResult& result);
const UserAsProbeResult& user_as_probe_result();

struct Ring3ProbeResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t enter;
    uint32_t int80;
    uint32_t cpl;
    uint32_t rsp0;
};

bool run_ring3_probe(Ring3ProbeResult& result);
const Ring3ProbeResult& ring3_probe_result();

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
