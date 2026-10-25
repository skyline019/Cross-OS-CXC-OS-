#include <cxc/smp.hpp>

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/pmm.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/smp_trampoline_blob.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::smp {
namespace {

constexpr uint64_t trampoline_page_count = 2;
constexpr uint64_t trampoline_bytes = trampoline_page_count * pmm::page_size;
constexpr uint64_t trampoline_limit = 0x100000;
constexpr uint64_t gdt_offset = 0x100;
constexpr uint64_t protected_entry_offset = 0x180;
constexpr uint32_t pit_ticks_10ms = 11932;
constexpr uint32_t pit_ticks_200us = 239;

struct alignas(64) CpuBlock {
    uint64_t self;
    uint32_t cpu_index;
    uint32_t apic_id;
    volatile uint32_t online;
    volatile uint32_t parked;
    volatile uint32_t error;
    uint32_t reserved;
    uint64_t cr3;
    volatile uint64_t ipi_payload;
    volatile uint64_t ipi_calls;
    volatile uint64_t last_ipi_payload;
    uint64_t stack_physical;
    uint64_t stack_base;
    volatile uint64_t scheduler_control_ipi_calls;
};

static_assert(offsetof(CpuBlock, self) == 0);
static_assert(offsetof(CpuBlock, cpu_index) == 8);

CpuBlock blocks[maximum_processors];
TestInfo info;
uint64_t trampoline_physical = 0;
uint32_t processor_total = 0;
volatile uint32_t online_total = 0;

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
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

uint64_t read_cr0() {
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
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

void write_gs_base(const CpuBlock& block) {
    write_msr(0xc0000101, reinterpret_cast<uint64_t>(&block));
}

void write16(uint8_t* base, uint64_t offset, uint16_t value) {
    base[offset] = static_cast<uint8_t>(value);
    base[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* base, uint64_t offset, uint32_t value) {
    for (uint32_t index = 0; index < 4; ++index) {
        base[offset + index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void write64(uint8_t* base, uint64_t offset, uint64_t value) {
    for (uint32_t index = 0; index < 8; ++index) {
        base[offset + index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

bool pit_wait(uint32_t ticks) {
    if (ticks == 0 || ticks > 0xffff) {
        return false;
    }
    const uint8_t port61 = inb(0x61);
    outb(0x61, (port61 & 0xfc) | 0x01);
    outb(0x43, 0xb0);
    outb(0x42, static_cast<uint8_t>(ticks));
    outb(0x42, static_cast<uint8_t>(ticks >> 8));
    for (uint32_t guard = 0; guard < 100000000; ++guard) {
        if ((inb(0x61) & 0x20) != 0) {
            return true;
        }
        cpu::relax();
    }
    return false;
}

bool wait_for_online(CpuBlock& block) {
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        if (__atomic_load_n(&block.online, __ATOMIC_ACQUIRE) == 1) {
            return true;
        }
        if (__atomic_load_n(&block.error, __ATOMIC_ACQUIRE) != 0 ||
            !pit_wait(pit_ticks_200us)) {
            return false;
        }
    }
    return false;
}

bool wait_for_ipi(CpuBlock& block, uint64_t expected) {
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        if (__atomic_load_n(&block.ipi_calls, __ATOMIC_ACQUIRE) == expected) {
            return true;
        }
        if (__atomic_load_n(&block.error, __ATOMIC_ACQUIRE) != 0 ||
            !pit_wait(pit_ticks_200us)) {
            return false;
        }
    }
    return false;
}

void publish_result(uint32_t index) {
    const CpuBlock& block = blocks[index];
    CpuResult& result = info.cpus[index];
    result.apic_id = block.apic_id;
    result.online = __atomic_load_n(&block.online, __ATOMIC_ACQUIRE);
    result.parked = __atomic_load_n(&block.parked, __ATOMIC_ACQUIRE);
    result.error = __atomic_load_n(&block.error, __ATOMIC_ACQUIRE);
    result.ipi_calls = __atomic_load_n(&block.ipi_calls, __ATOMIC_ACQUIRE);
    result.last_ipi_payload =
        __atomic_load_n(&block.last_ipi_payload, __ATOMIC_ACQUIRE);
    result.stack_physical = block.stack_physical;
    result.stack_base = block.stack_base;
    info.online_processors = __atomic_load_n(&online_total, __ATOMIC_ACQUIRE);
}

bool initialize_bootstrap() {
    const acpi::MadtInfo& madt = acpi::madt();
    if (!acpi::discovered() || madt.processor_count == 0 ||
        madt.processor_count > maximum_processors) {
        return false;
    }

    memset(blocks, 0, sizeof(blocks));
    processor_total = 0;
    __atomic_store_n(&online_total, 0, __ATOMIC_RELAXED);
    uint32_t bsp_index = madt.processor_count;
    for (uint32_t index = 0; index < madt.processor_count; ++index) {
        if (madt.processors[index].apic_id == apic::apic_id()) {
            bsp_index = index;
            break;
        }
    }
    if (bsp_index == madt.processor_count) {
        return false;
    }

    auto append = [&](const acpi::ProcessorDescriptor& descriptor) {
        CpuBlock& block = blocks[processor_total];
        block.self = reinterpret_cast<uint64_t>(&block);
        block.cpu_index = processor_total;
        block.apic_id = descriptor.apic_id;
        ++processor_total;
    };
    append(madt.processors[bsp_index]);
    for (uint32_t index = 0; index < madt.processor_count; ++index) {
        if (index != bsp_index) {
            append(madt.processors[index]);
        }
    }

    blocks[0].cr3 = read_cr3();
    __atomic_store_n(&blocks[0].online, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&online_total, 1, __ATOMIC_RELEASE);
    write_gs_base(blocks[0]);
    if (current_cpu_index() != 0 || blocks[0].self != read_msr(0xc0000101)) {
        return false;
    }
    info.expected_processors = processor_total;
    publish_result(0);
    return true;
}

bool map_and_verify_trampoline() {
    if (trampoline_physical < pmm::page_size ||
        trampoline_physical > trampoline_limit - trampoline_bytes ||
        (trampoline_physical & (pmm::page_size - 1)) != 0 ||
        trampoline_blob_size == 0 || trampoline_blob_size > pmm::page_size) {
        return false;
    }
    const uint64_t data_physical = trampoline_physical + pmm::page_size;
    if (!vmm::map_smp_trampoline(trampoline_physical, data_physical)) {
        return false;
    }

    const vmm::MappingInfo code =
        vmm::inspect_mapping(trampoline_physical);
    const vmm::MappingInfo data = vmm::inspect_mapping(data_physical);
    if (!code.present || code.writable || !code.executable ||
        !data.present || !data.writable || data.executable) {
        return false;
    }

    const auto* copied = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(trampoline_physical));
    for (size_t index = 0; index < trampoline_blob_size; ++index) {
        if (copied[index] != trampoline_blob[index]) {
            return false;
        }
    }
    auto* parameters = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(data_physical));
    memset(parameters, 0, pmm::page_size);
    return true;
}

[[noreturn]] void application_processor_entry(uint32_t cpu_index) {
    if (cpu_index == 0 || cpu_index >= processor_total) {
        cpu::halt();
    }
    CpuBlock& block = blocks[cpu_index];
    cpu::initialize_gdt();
    cpu::load_idt();
    write_gs_base(block);
    block.cr3 = read_cr3();
    if (current_cpu_index() != cpu_index || read_msr(0xc0000101) != block.self) {
        __atomic_store_n(&block.error, 1, __ATOMIC_RELEASE);
        cpu::halt();
    }
    if (!apic::initialize_application_processor(block.apic_id) ||
        apic::current_apic_id() != block.apic_id || block.cr3 == 0) {
        __atomic_store_n(&block.error, 2, __ATOMIC_RELEASE);
        cpu::halt();
    }

    // Parking is published before online so an acquired online=1 also proves
    // the AP reached its wait loop. M9 may temporarily leave this loop for a
    // per-CPU scheduler, but always returns with IF clear before parking again.
    __atomic_store_n(&block.parked, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&block.online, 1, __ATOMIC_RELEASE);
    cpu::enable_interrupts();
    for (;;) {
        cpu::wait_for_interrupt();
        if (scheduler::application_processors_should_start()) {
            __atomic_store_n(&block.parked, 0, __ATOMIC_RELEASE);
            scheduler::application_processor_main();
            __atomic_store_n(&block.parked, 1, __ATOMIC_RELEASE);
            cpu::enable_interrupts();
        }
    }
}

bool start_one(uint32_t index) {
    info.active_cpu = index;
    CpuBlock& block = blocks[index];
    const uint64_t stack_base = application_stack_region +
                                (index - 1) * application_stack_stride;
    info.stage = 10;
    const uint64_t stack_physical = pmm::allocate_pages(application_stack_pages);
    if (stack_physical == 0 ||
        !vmm::map_kernel_stack(stack_physical, stack_base,
                               application_stack_pages) ||
        vmm::is_mapped(stack_base - pmm::page_size)) {
        return false;
    }
    const vmm::MappingInfo stack = vmm::inspect_mapping(stack_base);
    if (!stack.present || !stack.writable || stack.executable) {
        return false;
    }
    block.stack_physical = stack_physical;
    block.stack_base = stack_base;

    info.stage = 11;
    const uint64_t cr0 = read_cr0();
    const uint64_t cr3 = read_cr3();
    const uint64_t cr4 = read_cr4();
    const uint64_t efer = read_msr(0xc0000080);
    if (cr3 != vmm::root_physical_address() || cr3 > 0xffffffffu ||
        (cr0 >> 32) != 0 || (cr4 >> 32) != 0) {
        return false;
    }
    auto* data = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(trampoline_physical + pmm::page_size));
    write16(data, 0x00, 4 * 8 - 1);
    write32(data, 0x02,
            static_cast<uint32_t>(trampoline_physical + gdt_offset));
    write32(data, 0x08,
            static_cast<uint32_t>(trampoline_physical + protected_entry_offset));
    write16(data, 0x0c, 0x08);
    write32(data, 0x10, static_cast<uint32_t>(cr3));
    write32(data, 0x14, static_cast<uint32_t>(cr4));
    write64(data, 0x18, efer);
    write32(data, 0x20, static_cast<uint32_t>(cr0));
    write32(data, 0x24, index);
    write64(data, 0x28, stack_base + application_stack_pages * pmm::page_size);
    write64(data, 0x30,
            reinterpret_cast<uint64_t>(&application_processor_entry));
    __asm__ volatile("mfence" : : : "memory");

    info.stage = 12;
    if (!apic::send_init_ipi(block.apic_id) || !pit_wait(pit_ticks_10ms)) {
        return false;
    }
    info.stage = 13;
    const uint8_t page = static_cast<uint8_t>(trampoline_physical >> 12);
    if (!apic::send_startup_ipi(block.apic_id, page) ||
        !pit_wait(pit_ticks_200us)) {
        return false;
    }
    if (__atomic_load_n(&block.online, __ATOMIC_ACQUIRE) == 0 &&
        __atomic_load_n(&block.error, __ATOMIC_ACQUIRE) == 0) {
        ++info.second_sipi_count;
        if (!apic::send_startup_ipi(block.apic_id, page)) {
            return false;
        }
    }

    info.stage = 14;
    if (!wait_for_online(block) ||
        __atomic_load_n(&block.parked, __ATOMIC_ACQUIRE) != 1 ||
        block.cr3 != cr3 || apic::current_apic_id() != apic::apic_id()) {
        publish_result(index);
        return false;
    }
    __atomic_add_fetch(&online_total, 1, __ATOMIC_ACQ_REL);
    publish_result(index);

    for (uint64_t sequence = 1; sequence <= 2; ++sequence) {
        info.stage = 14 + static_cast<uint32_t>(sequence);
        const uint64_t payload = 0x4d38000000000000ull |
                                 (sequence << 16) | index;
        __atomic_store_n(&block.ipi_payload, payload, __ATOMIC_RELEASE);
        if (!apic::send_fixed_ipi(block.apic_id, cpu::ipi_vector) ||
            !wait_for_ipi(block, sequence) ||
            __atomic_load_n(&block.last_ipi_payload, __ATOMIC_ACQUIRE) !=
                payload) {
            publish_result(index);
            return false;
        }
        publish_result(index);
    }
    return true;
}

}  // namespace

bool prepare_trampoline(uint64_t code_physical) {
    if (code_physical < pmm::page_size ||
        code_physical > trampoline_limit - trampoline_bytes ||
        (code_physical & (pmm::page_size - 1)) != 0 ||
        trampoline_blob_size == 0 || trampoline_blob_size > pmm::page_size) {
        return false;
    }
    auto* code = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(code_physical));
    memset(code, 0, trampoline_bytes);
    memcpy(code, trampoline_blob, trampoline_blob_size);
    trampoline_physical = code_physical;
    return true;
}

bool run() {
    info = TestInfo{};
    info.trampoline_physical = trampoline_physical;
    info.stage = 1;
    if (cpu::interrupts_enabled() || !apic::initialized() ||
        !initialize_bootstrap()) {
        return false;
    }
    if (processor_total == 1) {
        info.single_cpu = 1;
        info.stage = 20;
        return true;
    }

    info.stage = 2;
    if (!map_and_verify_trampoline()) {
        return false;
    }
    for (uint32_t index = 1; index < processor_total; ++index) {
        if (!start_one(index)) {
            return false;
        }
    }

    info.total_ipi_calls = 0;
    for (uint32_t index = 0; index < processor_total; ++index) {
        publish_result(index);
        if (__atomic_load_n(&blocks[index].online, __ATOMIC_ACQUIRE) != 1 ||
            (index != 0 &&
             __atomic_load_n(&blocks[index].ipi_calls, __ATOMIC_ACQUIRE) != 2)) {
            return false;
        }
        info.total_ipi_calls +=
            __atomic_load_n(&blocks[index].ipi_calls, __ATOMIC_ACQUIRE);
    }
    if (current_cpu_index() != 0 ||
        __atomic_load_n(&online_total, __ATOMIC_ACQUIRE) != processor_total) {
        return false;
    }
    info.active_cpu = 0;
    info.stage = 20;
    return true;
}

bool signal_application_processors() {
    if (processor_total == 0 || current_cpu_index() != 0) {
        return false;
    }
    for (uint32_t index = 1; index < processor_total; ++index) {
        if (!apic::send_fixed_ipi(blocks[index].apic_id,
                                  cpu::scheduler_ipi_vector)) {
            return false;
        }
    }
    return true;
}

bool signal_processor(uint32_t cpu_index, uint8_t vector) {
    if (cpu_index >= processor_total ||
        __atomic_load_n(&blocks[cpu_index].online, __ATOMIC_ACQUIRE) == 0) {
        return false;
    }
    return apic::send_fixed_ipi(blocks[cpu_index].apic_id, vector);
}

uint64_t scheduler_control_ipi_count(uint32_t cpu_index) {
    return cpu_index < processor_total
               ? __atomic_load_n(&blocks[cpu_index].scheduler_control_ipi_calls,
                                 __ATOMIC_ACQUIRE)
               : 0;
}

uint32_t apic_id_for_cpu(uint32_t cpu_index) {
    return cpu_index < processor_total ? blocks[cpu_index].apic_id : 0xffffffffu;
}

bool verify_parked_processors() {
    if (cpu::interrupts_enabled() || current_cpu_index() != 0 ||
        __atomic_load_n(&online_total, __ATOMIC_ACQUIRE) != processor_total) {
        return false;
    }
    for (uint32_t index = 1; index < processor_total; ++index) {
        info.active_cpu = index;
        info.stage = 30;
        CpuBlock& block = blocks[index];
        const uint64_t payload = 0x4d38000000000000ull | (3ull << 16) | index;
        __atomic_store_n(&block.ipi_payload, payload, __ATOMIC_RELEASE);
        if (!apic::send_fixed_ipi(block.apic_id, cpu::ipi_vector) ||
            !wait_for_ipi(block, 3) ||
            __atomic_load_n(&block.last_ipi_payload, __ATOMIC_ACQUIRE) !=
                payload ||
            __atomic_load_n(&block.parked, __ATOMIC_ACQUIRE) != 1) {
            publish_result(index);
            return false;
        }
        publish_result(index);
    }

    info.total_ipi_calls = 0;
    for (uint32_t index = 0; index < processor_total; ++index) {
        info.total_ipi_calls +=
            __atomic_load_n(&blocks[index].ipi_calls, __ATOMIC_ACQUIRE);
    }
    info.active_cpu = 0;
    info.stage = 20;
    return true;
}

const TestInfo& test_info() {
    return info;
}

uint32_t current_cpu_index() {
    uint32_t index;
    __asm__ volatile("movl %%gs:8, %0" : "=r"(index));
    return index;
}

uint32_t online_processor_count() {
    return __atomic_load_n(&online_total, __ATOMIC_ACQUIRE);
}

void on_ipi() {
    const uint32_t index = current_cpu_index();
    if (index >= processor_total) {
        return;
    }
    CpuBlock& block = blocks[index];
    const uint64_t payload =
        __atomic_load_n(&block.ipi_payload, __ATOMIC_ACQUIRE);
    __atomic_store_n(&block.last_ipi_payload, payload, __ATOMIC_RELAXED);
    __atomic_add_fetch(&block.ipi_calls, 1, __ATOMIC_RELEASE);
}

void on_scheduler_ipi() {
    const uint32_t index = current_cpu_index();
    if (index < processor_total) {
        __atomic_add_fetch(&blocks[index].scheduler_control_ipi_calls, 1,
                           __ATOMIC_RELEASE);
    }
}

}  // namespace cxc::smp
