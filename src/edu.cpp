#include <cxc/edu.hpp>

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/irq.hpp>
#include <cxc/pci.hpp>
#include <cxc/pmm.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" int memcmp(const void* left, const void* right, size_t size);
extern "C" void* memset(void* destination, int value, size_t size);

namespace cxc::edu {
namespace {

constexpr uint16_t edu_vendor = 0x1234;
constexpr uint16_t edu_device = 0x11e8;
constexpr uint64_t mmio_virtual_base = 0xffffffff40001000;
constexpr uint64_t dma_limit = uint64_t{1} << 28;
constexpr uint32_t dma_size = 256;
constexpr uint64_t wait_ticks = 1000;

constexpr uint32_t reg_identification = 0x00;
constexpr uint32_t reg_liveness = 0x04;
constexpr uint32_t reg_irq_status = 0x24;
constexpr uint32_t reg_irq_raise = 0x60;
constexpr uint32_t reg_irq_acknowledge = 0x64;
constexpr uint32_t reg_dma_source = 0x80;
constexpr uint32_t reg_dma_destination = 0x88;
constexpr uint32_t reg_dma_count = 0x90;
constexpr uint32_t reg_dma_command = 0x98;
constexpr uint64_t edu_dma_buffer = 0x40000;

constexpr uint32_t dma_start = 0x01;
constexpr uint32_t dma_from_device = 0x02;
constexpr uint32_t dma_interrupt = 0x04;
constexpr uint32_t irq_direct = 0x001;
constexpr uint32_t irq_dma = 0x100;

constexpr uint16_t command_memory = 1 << 1;
constexpr uint16_t command_bus_master = 1 << 2;
constexpr uint16_t command_intx_disable = 1 << 10;
constexpr uint16_t msi_enable = 1 << 0;
constexpr uint16_t msi_multiple_message_enable = 0x70;
constexpr uint16_t msi_64bit = 1 << 7;
constexpr uint16_t msi_per_vector_masking = 1 << 8;

TestInfo info;
volatile uint32_t unexpected_status = 0;

uint32_t mmio_read(uint32_t offset) {
    const auto* address = reinterpret_cast<volatile const uint32_t*>(
        static_cast<uintptr_t>(mmio_virtual_base + offset));
    const uint32_t value = *address;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return value;
}

void mmio_write(uint32_t offset, uint32_t value) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    auto* address = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(mmio_virtual_base + offset));
    *address = value;
}

void write64(uint32_t offset, uint64_t value) {
    mmio_write(offset, static_cast<uint32_t>(value));
    mmio_write(offset + 4, static_cast<uint32_t>(value >> 32));
}

void interrupt_handler(void*) {
    const uint32_t status = mmio_read(reg_irq_status);
    __atomic_add_fetch(&info.irq_calls, 1, __ATOMIC_RELAXED);
    if ((status & irq_direct) != 0) {
        __atomic_add_fetch(&info.direct_interrupts, 1, __ATOMIC_RELAXED);
    }
    if ((status & irq_dma) != 0) {
        __atomic_add_fetch(&info.dma_interrupts, 1, __ATOMIC_RELAXED);
    }
    if (status == 0 || (status & ~(irq_direct | irq_dma)) != 0) {
        __atomic_add_fetch(&unexpected_status, 1, __ATOMIC_RELAXED);
    }

    // EDU requires this acknowledge even when the interrupt arrived by MSI.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write(reg_irq_acknowledge, status);
}

bool wait_for_calls(uint64_t target, uint64_t first_tick) {
    while (__atomic_load_n(&info.irq_calls, __ATOMIC_ACQUIRE) < target) {
        if (scheduler::ticks() - first_tick > wait_ticks) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    return true;
}

void start_dma(uint64_t source, uint64_t destination, bool from_device) {
    write64(reg_dma_source, source);
    write64(reg_dma_destination, destination);
    mmio_write(reg_dma_count, dma_size);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mmio_write(reg_dma_command,
               dma_start | dma_interrupt |
                   (from_device ? dma_from_device : 0));
}

}  // namespace

bool run_msi_dma_test() {
    if (cpu::interrupts_enabled() || !apic::initialized() ||
        apic::apic_id() > 0xff) {
        return false;
    }

    info = TestInfo{};
    __atomic_store_n(&unexpected_status, 0, __ATOMIC_RELAXED);
    const uint64_t unhandled_before = irq::unhandled_count();
    const pci::Device* device = nullptr;
    uint8_t capability = 0;
    uint8_t vector = 0;
    uint16_t original_command = 0;
    uint16_t original_control = 0;
    uint64_t source_physical = 0;
    uint64_t destination_physical = 0;
    uint64_t dma_free_before = 0;
    bool check_free_accounting = false;
    bool mapped = false;
    bool command_changed = false;
    bool registered = false;
    bool timer_started = false;
    bool success = false;

    // A single explicit cleanup path avoids freestanding RAII/unwind runtime
    // dependencies while still quiescing the device on every failure.
    do {
        for (size_t index = 0; index < pci::device_count(); ++index) {
            const pci::Device& candidate = pci::devices()[index];
            if (candidate.vendor_id == edu_vendor &&
                candidate.device_id == edu_device) {
                device = &candidate;
                break;
            }
        }
        if (device == nullptr) {
            break;
        }
        capability = pci::find_capability(*device, 0x05);
        if (capability == 0) {
            break;
        }

        const uint32_t bar0 = pci::read_bar(*device, 0);
        if ((bar0 & 1) != 0 || (bar0 & 6) != 0) {
            break;  // EDU BAR0 must be 32-bit memory, not I/O or a 64-bit BAR.
        }
        const uint64_t bar_physical = bar0 & 0xfffffff0u;
        if (bar_physical == 0 || vmm::is_mapped(mmio_virtual_base) ||
            !vmm::map_device_page(bar_physical, mmio_virtual_base)) {
            break;
        }
        mapped = true;
        info.mmio_physical = bar_physical;
        const vmm::MappingInfo mmio_mapping =
            vmm::inspect_mapping(mmio_virtual_base);
        if (!mmio_mapping.present || !mmio_mapping.writable ||
            mmio_mapping.executable) {
            break;
        }

        original_command = pci::config_read16(*device, 0x04);
        pci::config_write16(*device, 0x04,
                            original_command | command_memory |
                                command_intx_disable);
        command_changed = true;

        info.identification = mmio_read(reg_identification);
        mmio_write(reg_liveness, 0x12345678);
        if ((info.identification & 0xffff) != 0x00ed ||
            mmio_read(reg_liveness) != 0xedcba987) {
            break;
        }

        dma_free_before = pmm::free_pages_count();
        check_free_accounting = true;
        source_physical = pmm::allocate_pages_below(1, dma_limit);
        destination_physical = pmm::allocate_pages_below(1, dma_limit);
        if (source_physical == 0 || destination_physical == 0 ||
            source_physical + pmm::page_size > dma_limit ||
            destination_physical + pmm::page_size > dma_limit) {
            break;
        }
        info.source_physical = source_physical;
        info.destination_physical = destination_physical;

        if (!irq::register_message(interrupt_handler, nullptr, vector)) {
            break;
        }
        registered = true;
        info.vector = vector;

        pci::config_write16(*device, 0x04,
                            original_command | command_memory |
                                command_bus_master | command_intx_disable);

        original_control = pci::config_read16(*device, capability + 2);
        pci::config_write16(*device, capability + 2,
                            original_control & ~msi_enable);
        if ((original_control & msi_per_vector_masking) != 0) {
            const uint8_t mask_offset = capability +
                ((original_control & msi_64bit) != 0 ? 16 : 12);
            pci::config_write32(*device, mask_offset, 1);
        }

        // Non-remapped MSI supports the low eight APIC ID bits checked above.
        // QEMU's BSP is APIC ID 0 in both xAPIC and x2APIC test modes.
        info.msi_address = 0xfee00000u | (apic::apic_id() << 12);
        info.msi_data = vector;
        pci::config_write32(*device, capability + 4, info.msi_address);
        if ((original_control & msi_64bit) != 0) {
            pci::config_write32(*device, capability + 8, 0);
            pci::config_write16(*device, capability + 12, info.msi_data);
        } else {
            pci::config_write16(*device, capability + 8, info.msi_data);
        }
        pci::config_write16(*device, capability + 2,
                            (original_control & ~msi_multiple_message_enable) |
                                msi_enable);
        if ((original_control & msi_per_vector_masking) != 0) {
            const uint8_t mask_offset = capability +
                ((original_control & msi_64bit) != 0 ? 16 : 12);
            pci::config_write32(*device, mask_offset, 0);
        }

        const uint32_t pending = mmio_read(reg_irq_status);
        if (pending != 0) {
            mmio_write(reg_irq_acknowledge, pending);
        }
        auto* source = static_cast<uint8_t*>(pmm::physical_to_virtual(source_physical));
        auto* destination =
            static_cast<uint8_t*>(pmm::physical_to_virtual(destination_physical));
        for (uint32_t index = 0; index < dma_size; ++index) {
            source[index] = static_cast<uint8_t>(index * 37 + 11);
        }
        memset(destination, 0xa5, pmm::page_size);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        apic::start_periodic();
        timer_started = true;
        const uint64_t first_tick = scheduler::ticks();
        cpu::enable_interrupts();

        // A software-generated device event must arrive as a real MSI on the
        // registered IDT vector; the ISR reads and acknowledges device status.
        mmio_write(reg_irq_raise, irq_direct);
        if (!wait_for_calls(1, first_tick) ||
            __atomic_load_n(&info.direct_interrupts, __ATOMIC_ACQUIRE) != 1) {
            break;
        }

        start_dma(source_physical, edu_dma_buffer, false);
        if (!wait_for_calls(2, first_tick) ||
            __atomic_load_n(&info.dma_interrupts, __ATOMIC_ACQUIRE) != 1 ||
            (mmio_read(reg_dma_command) & dma_start) != 0) {
            break;
        }

        start_dma(edu_dma_buffer, destination_physical, true);
        if (!wait_for_calls(3, first_tick) ||
            __atomic_load_n(&info.dma_interrupts, __ATOMIC_ACQUIRE) != 2 ||
            (mmio_read(reg_dma_command) & dma_start) != 0) {
            break;
        }

        cpu::disable_interrupts();
        apic::stop_periodic();
        timer_started = false;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        bool guard_intact = true;
        for (size_t index = dma_size; index < pmm::page_size; ++index) {
            if (destination[index] != 0xa5) {
                guard_intact = false;
                break;
            }
        }
        success = memcmp(source, destination, dma_size) == 0 && guard_intact &&
                  __atomic_load_n(&info.irq_calls, __ATOMIC_ACQUIRE) == 3 &&
                  __atomic_load_n(&info.direct_interrupts, __ATOMIC_ACQUIRE) == 1 &&
                  __atomic_load_n(&info.dma_interrupts, __ATOMIC_ACQUIRE) == 2 &&
                  __atomic_load_n(&unexpected_status, __ATOMIC_ACQUIRE) == 0 &&
                  mmio_read(reg_irq_status) == 0;
        if (success) {
            info.dma_bytes = dma_size;
        }
    } while (false);

    cpu::disable_interrupts();
    if (timer_started) {
        apic::stop_periodic();
    }

    bool cleanup_ok = true;
    if (device != nullptr && capability != 0) {
        const uint16_t control = pci::config_read16(*device, capability + 2);
        pci::config_write16(*device, capability + 2, control & ~msi_enable);
        if ((original_control & msi_per_vector_masking) != 0) {
            const uint8_t mask_offset = capability +
                ((original_control & msi_64bit) != 0 ? 16 : 12);
            pci::config_write32(*device, mask_offset, 1);
        }
    }
    if (mapped) {
        const uint32_t pending = mmio_read(reg_irq_status);
        if (pending != 0) {
            mmio_write(reg_irq_acknowledge, pending);
        }
    }
    if (command_changed) {
        pci::config_write16(*device, 0x04,
                            (original_command & ~command_bus_master) |
                                command_intx_disable);
    }
    if (registered && !irq::unregister(vector)) {
        cleanup_ok = false;
    }
    if (source_physical != 0) {
        pmm::free_pages(source_physical, 1);
    }
    if (destination_physical != 0) {
        pmm::free_pages(destination_physical, 1);
    }

    return success && cleanup_ok &&
           (!check_free_accounting ||
            pmm::free_pages_count() == dma_free_before) &&
           irq::unhandled_count() == unhandled_before;
}

const TestInfo& test_info() {
    return info;
}

}  // namespace cxc::edu
