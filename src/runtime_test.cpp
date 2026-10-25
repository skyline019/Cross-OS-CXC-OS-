#include <cxc/runtime_test.hpp>

#include <cxc/apic.hpp>
#include <cxc/cpu.hpp>
#include <cxc/heap.hpp>
#include <cxc/log.hpp>
#include <cxc/net/datagram.hpp>
#include <cxc/net/proto.hpp>
#include <cxc/net/socket.hpp>
#include <cxc/pmm.hpp>
#include <cxc/scheduler.hpp>
#include <cxc/smp.hpp>
#include <cxc/spinlock.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" bool runtime_write_probe(void* address, uint64_t value);
extern "C" void runtime_write_probe_instruction();
extern "C" void runtime_write_probe_recovery();

namespace cxc::runtime_test {
namespace {

constexpr uint64_t worker_iterations = 32;
constexpr size_t maximum_workers = smp::maximum_processors + 1;

SpinLock shared_lock;
uint64_t shared_counter = 0;
volatile uint64_t progress[maximum_workers];
volatile uint64_t observed_cpu[maximum_workers];
volatile uint32_t visited_cpu_mask[maximum_workers];
volatile bool worker_fault = false;
volatile bool affinity_fault = false;
volatile bool lock_if_fault = false;
volatile bool critical_overlap_fault = false;
volatile bool sleep_fault = false;
volatile bool migration_test_fault = false;
volatile uint32_t critical_active = 0;
volatile uint64_t sleep_calls = 0;
volatile uint64_t observed_sleep_ticks = 0;
volatile uint32_t arrived_workers = 0;
volatile uint32_t arrival_cpu_mask = 0;
uint32_t expected_workers = 0;

constexpr uint32_t memory_rounds = 8;
constexpr size_t heap_probe_size = 96;
volatile uint64_t memory_pages[maximum_workers];
volatile uint64_t memory_blocks[maximum_workers];
volatile uint32_t memory_allocated = 0;
volatile uint32_t memory_verified = 0;
volatile uint32_t memory_released = 0;
volatile uint32_t memory_cpu_mask = 0;
volatile uint64_t runtime_pmm_allocations = 0;
volatile uint64_t runtime_heap_allocations = 0;
volatile bool memory_test_fault = false;

constexpr uint64_t test_page_base = 0xffffffff68000000;
volatile uint32_t tlb_ready = 0;
volatile uint32_t tlb_probed = 0;
volatile uint32_t tlb_phase = 0;
volatile uint32_t probe_cpu_mask = 0;
volatile uint32_t probe_write_mask = 0;
volatile uint64_t probe_faults = 0;
volatile bool tlb_test_fault = false;
volatile uint32_t recycle_progress = 0;
volatile bool recycle_fault = false;
volatile bool event_fault = false;
volatile uint32_t event_wait_done = 0;
volatile uint32_t event_signal_done = 0;
volatile uint32_t queue_wait_ready = 0;
volatile uint32_t queue_wait_done = 0;
volatile uint32_t queue_broadcast_done = 0;
volatile uint32_t mutex_critical = 0;
volatile bool mutex_fault = false;
volatile uint32_t mutex_enter_ready = 0;
uint64_t mutex_shared_counter = 0;
constexpr uint32_t mutex_rounds = 16;
volatile bool socket_fault = false;
volatile uint32_t socket_wait_ready = 0;
volatile uint32_t socket_wait_done = 0;
volatile uint32_t socket_deliver_done = 0;
net::UdpSocketTable socket_table{};
int socket_slot = -1;
constexpr uint16_t socket_test_port = 0x4D22;
constexpr uint8_t socket_test_payload[] = {0x4D, 0x32, 0x32};
scheduler::Event lifecycle_event{};
scheduler::Event queue_event{};
scheduler::Mutex lifecycle_mutex{};

struct WorkerArgument {
    uint64_t id;
    uint32_t cpu_index;
    uint32_t reserved;
};

WorkerArgument worker_arguments[maximum_workers];
WorkerArgument recycle_arguments[maximum_workers];

void recycle_worker_entry(void* opaque) {
    const auto* argument = static_cast<const WorkerArgument*>(opaque);
    if (argument == nullptr ||
        smp::current_cpu_index() != argument->cpu_index) {
        __atomic_store_n(&recycle_fault, true, __ATOMIC_RELEASE);
        return;
    }
    __atomic_add_fetch(&recycle_progress, 1, __ATOMIC_ACQ_REL);
}

void require(bool condition, const char* message) {
    if (!condition) {
        log::panic(message);
    }
}

void set_worker_fault() {
    __atomic_store_n(&worker_fault, true, __ATOMIC_RELEASE);
}

bool wait_for_count(volatile uint32_t& counter, uint32_t expected) {
    while (__atomic_load_n(&counter, __ATOMIC_ACQUIRE) < expected) {
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE) ||
            !scheduler::started()) {
            return false;
        }
        cpu::wait_for_interrupt();
    }
    return true;
}

uint64_t memory_pattern(size_t id, uint32_t round) {
    return 0x435850000000005aull | ((id + 1) << 32) | ((round + 1) << 8);
}

uint8_t heap_pattern(size_t id, uint32_t round) {
    return static_cast<uint8_t>(0x40 + id * 7 + round);
}

uint64_t tlb_pattern(size_t id, uint32_t phase) {
    return 0x544c420000000000ull | ((id + 1) << 16) | phase;
}

bool exercise_memory_allocators(size_t id) {
    for (uint32_t round = 0; round < memory_rounds; ++round) {
        const uint32_t completed = round + 1;
        const uint64_t page = pmm::allocate_pages(1);
        if (page == 0) {
            __atomic_store_n(&memory_test_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return false;
        }
        __atomic_add_fetch(&runtime_pmm_allocations, 1, __ATOMIC_RELAXED);
        auto* block = static_cast<uint8_t*>(heap::allocate(heap_probe_size));
        if (block == nullptr) {
            pmm::free_pages(page, 1);
            __atomic_store_n(&memory_test_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return false;
        }
        __atomic_add_fetch(&runtime_heap_allocations, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&memory_pages[id], page, __ATOMIC_RELEASE);
        __atomic_store_n(&memory_blocks[id], reinterpret_cast<uint64_t>(block),
                         __ATOMIC_RELEASE);
        __atomic_or_fetch(&memory_cpu_mask,
                          uint32_t{1} << smp::current_cpu_index(),
                          __ATOMIC_ACQ_REL);

        auto* page_words = static_cast<volatile uint64_t*>(
            pmm::physical_to_virtual(page));
        const uint64_t expected_page = memory_pattern(id, round);
        page_words[0] = expected_page;
        page_words[pmm::page_size / sizeof(uint64_t) - 1] = expected_page;
        const uint8_t expected_byte = heap_pattern(id, round);
        for (size_t index = 0; index < heap_probe_size; ++index) {
            block[index] = expected_byte;
        }

        __atomic_add_fetch(&memory_allocated, 1, __ATOMIC_ACQ_REL);
        if (!wait_for_count(memory_allocated,
                            expected_workers * completed)) {
            return false;
        }
        for (size_t other = 0; other < expected_workers; ++other) {
            const uint64_t other_page =
                __atomic_load_n(&memory_pages[other], __ATOMIC_ACQUIRE);
            const auto* other_words = static_cast<const volatile uint64_t*>(
                pmm::physical_to_virtual(other_page));
            const auto* other_block = reinterpret_cast<const uint8_t*>(
                static_cast<uintptr_t>(__atomic_load_n(&memory_blocks[other],
                                                       __ATOMIC_ACQUIRE)));
            const uint64_t expected_other = memory_pattern(other, round);
            const uint8_t expected_other_byte = heap_pattern(other, round);
            if (other_page == 0 || other_block == nullptr ||
                (other != id && other_page == page) ||
                other_words[0] != expected_other ||
                other_words[pmm::page_size / sizeof(uint64_t) - 1] !=
                    expected_other) {
                __atomic_store_n(&memory_test_fault, true, __ATOMIC_RELEASE);
                set_worker_fault();
                return false;
            }
            for (size_t index = 0; index < heap_probe_size; ++index) {
                if (other_block[index] != expected_other_byte) {
                    __atomic_store_n(&memory_test_fault, true, __ATOMIC_RELEASE);
                    set_worker_fault();
                    return false;
                }
            }
        }

        __atomic_add_fetch(&memory_verified, 1, __ATOMIC_ACQ_REL);
        if (!wait_for_count(memory_verified,
                            expected_workers * completed)) {
            return false;
        }
        pmm::free_pages(page, 1);
        heap::release(block);
        __atomic_store_n(&memory_pages[id], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&memory_blocks[id], 0, __ATOMIC_RELEASE);
        __atomic_add_fetch(&memory_released, 1, __ATOMIC_ACQ_REL);
        if (!wait_for_count(memory_released,
                            expected_workers * completed)) {
            return false;
        }
    }
    return true;
}

bool exercise_tlb_shootdown(size_t id) {
    auto* const slot = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(test_page_base + id * sizeof(uint64_t)));
    const uint64_t initial = tlb_pattern(id, 0);
    // Populate/write through this CPU's TLB immediately before the permission
    // transition. All workers must arrive before the BSP can make it read-only.
    *slot = initial;
    if (*slot != initial) {
        __atomic_store_n(&tlb_test_fault, true, __ATOMIC_RELEASE);
        set_worker_fault();
        return false;
    }
    __atomic_add_fetch(&tlb_ready, 1, __ATOMIC_ACQ_REL);
    while (__atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) < 1) {
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE) ||
            !scheduler::started()) {
            return false;
        }
        cpu::wait_for_interrupt();
    }

    const uint64_t read_only_value = tlb_pattern(id, 1);
    if (runtime_write_probe(const_cast<uint64_t*>(slot), read_only_value) ||
        *slot != initial) {
        __atomic_store_n(&tlb_test_fault, true, __ATOMIC_RELEASE);
        set_worker_fault();
        return false;
    }
    __atomic_add_fetch(&tlb_probed, 1, __ATOMIC_ACQ_REL);
    while (__atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) < 2) {
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE) ||
            !scheduler::started()) {
            return false;
        }
        cpu::wait_for_interrupt();
    }

    const uint64_t restored_value = tlb_pattern(id, 2);
    if (!runtime_write_probe(const_cast<uint64_t*>(slot), restored_value) ||
        *slot != restored_value) {
        __atomic_store_n(&tlb_test_fault, true, __ATOMIC_RELEASE);
        set_worker_fault();
        return false;
    }
    __atomic_or_fetch(&probe_write_mask,
                      uint32_t{1} << smp::current_cpu_index(),
                      __ATOMIC_ACQ_REL);
    return true;
}

void worker_entry(void* opaque) {
    const auto* argument = static_cast<const WorkerArgument*>(opaque);
    const uint64_t id = argument->id;
    const uint32_t expected_cpu = argument->cpu_index;
    if (id >= expected_workers || expected_cpu >= smp::maximum_processors ||
        smp::current_cpu_index() != expected_cpu) {
        __atomic_store_n(&affinity_fault, true, __ATOMIC_RELEASE);
        set_worker_fault();
        return;
    }
    uint32_t running_cpu = expected_cpu;
    const uint64_t stack_base = scheduler::current_thread_stack_base();
    if (stack_base == 0) {
        __atomic_store_n(&migration_test_fault, true, __ATOMIC_RELEASE);
        set_worker_fault();
        return;
    }
    __atomic_store_n(&observed_cpu[id], expected_cpu + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&visited_cpu_mask[id], uint32_t{1} << expected_cpu,
                     __ATOMIC_RELEASE);
    __atomic_or_fetch(&arrival_cpu_mask, uint32_t{1} << expected_cpu,
                      __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&arrived_workers, 1, __ATOMIC_ACQ_REL);

    // No worker can finish before every CPU has a live worker at this barrier.
    // On one CPU the local timer still preempts worker 0 so worker 1 can arrive.
    while (__atomic_load_n(&arrived_workers, __ATOMIC_ACQUIRE) < expected_workers) {
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE)) {
            return;
        }
        cpu::wait_for_interrupt();
    }

    // M16: worker 0 parks; worker 1 delays then signals (Waiting→Ready).
    if (id == 0) {
        if (!scheduler::wait(lifecycle_event)) {
            __atomic_store_n(&event_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        __atomic_store_n(&event_wait_done, 1, __ATOMIC_RELEASE);
    } else if (id == 1) {
        if (!scheduler::sleep(4) || !scheduler::signal(lifecycle_event)) {
            __atomic_store_n(&event_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        __atomic_store_n(&event_signal_done, 1, __ATOMIC_RELEASE);
    }

    // M17: two workers enqueue on queue_event; BSP bootstrap broadcasts.
    const uint64_t queue_peer =
        expected_workers > 2 ? expected_workers - 1 : 1;
    if (id == 0 || id == queue_peer) {
        __atomic_add_fetch(&queue_wait_ready, 1, __ATOMIC_ACQ_REL);
        if (!scheduler::wait(queue_event)) {
            __atomic_store_n(&event_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        __atomic_add_fetch(&queue_wait_done, 1, __ATOMIC_ACQ_REL);
    }

    // M18: every worker contends on a blocking mutex around a shared counter.
    // Barrier + held-lock sleep so UP (2 workers, 1 CPU) still produces waits.
    __atomic_add_fetch(&mutex_enter_ready, 1, __ATOMIC_ACQ_REL);
    while (__atomic_load_n(&mutex_enter_ready, __ATOMIC_ACQUIRE) <
           expected_workers) {
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE)) {
            return;
        }
        cpu::wait_for_interrupt();
    }
    for (uint32_t round = 0; round < mutex_rounds; ++round) {
        if (!scheduler::mutex_lock(lifecycle_mutex)) {
            __atomic_store_n(&mutex_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        if (__atomic_add_fetch(&mutex_critical, 1, __ATOMIC_ACQ_REL) != 1) {
            __atomic_store_n(&mutex_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            (void)scheduler::mutex_unlock(lifecycle_mutex);
            return;
        }
        ++mutex_shared_counter;
        // Only the first two rounds sleep while holding the lock: enough for
        // UP to observe waits without stretching 16-vCPU contract timeouts.
        if (round < 2) {
            if (!scheduler::sleep(1)) {
                __atomic_store_n(&mutex_fault, true, __ATOMIC_RELEASE);
                set_worker_fault();
                (void)scheduler::mutex_unlock(lifecycle_mutex);
                return;
            }
        } else {
            for (uint32_t delay = 0; delay < 16; ++delay) {
                cpu::relax();
            }
        }
        __atomic_sub_fetch(&mutex_critical, 1, __ATOMIC_ACQ_REL);
        if (!scheduler::mutex_unlock(lifecycle_mutex)) {
            __atomic_store_n(&mutex_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
    }

    // M22: worker 0 blocks in udp_recv_wait; worker 1 delivers after ready.
    if (id == 0) {
        __atomic_store_n(&socket_wait_ready, 1, __ATOMIC_RELEASE);
        net::UdpQueuedDatagram got{};
        if (!net::udp_recv_wait(socket_table, static_cast<size_t>(socket_slot),
                                got) ||
            got.dst_port != socket_test_port ||
            got.payload_length != sizeof(socket_test_payload) ||
            got.payload[0] != socket_test_payload[0] ||
            got.payload[1] != socket_test_payload[1] ||
            got.payload[2] != socket_test_payload[2]) {
            __atomic_store_n(&socket_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        __atomic_store_n(&socket_wait_done, 1, __ATOMIC_RELEASE);
    } else if (id == 1) {
        while (__atomic_load_n(&socket_wait_ready, __ATOMIC_ACQUIRE) == 0) {
            if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE)) {
                return;
            }
            cpu::wait_for_interrupt();
        }
        if (!scheduler::sleep(4)) {
            __atomic_store_n(&socket_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        static constexpr uint8_t local_mac[6] = {0x52, 0x54, 0x00, 0x43, 0x58,
                                                0x43};
        static constexpr uint8_t remote_mac[6] = {0x02, 0x43, 0x58, 0x43, 0x11,
                                                 0x01};
        static constexpr uint8_t local_ipv4[4] = {10, 0, 12, 2};
        static constexpr uint8_t remote_ipv4[4] = {10, 0, 12, 1};
        uint8_t frame[64];
        const net::UdpWireEndpoints from_peer{remote_mac, local_mac,
                                              remote_ipv4, local_ipv4};
        const uint16_t built = net::build_udp_datagram(
            frame, sizeof(frame), from_peer, net::peer_udp_port,
            socket_test_port, 0x4D22, socket_test_payload,
            sizeof(socket_test_payload));
        if (built == 0 || !net::udp_deliver(socket_table, frame, built)) {
            __atomic_store_n(&socket_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        __atomic_store_n(&socket_deliver_done, 1, __ATOMIC_RELEASE);
    }

    for (uint64_t iteration = 0; iteration < worker_iterations; ++iteration) {
        if (id == 1 && expected_cpu == 0 && expected_workers > 2 &&
            (iteration == 8 || iteration == 24)) {
            const uint32_t target = iteration == 8 ? 1u : 0u;
            if (!scheduler::migrate_to(target)) {
                __atomic_store_n(&migration_test_fault, true, __ATOMIC_RELEASE);
                set_worker_fault();
                return;
            }
            running_cpu = target;
            __atomic_or_fetch(&visited_cpu_mask[id], uint32_t{1} << target,
                              __ATOMIC_ACQ_REL);
        }
        if (scheduler::current_thread_stack_base() != stack_base) {
            __atomic_store_n(&migration_test_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }
        if (smp::current_cpu_index() != running_cpu ||
            __atomic_load_n(&progress[id], __ATOMIC_ACQUIRE) != iteration) {
            __atomic_store_n(&affinity_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }

        const uint64_t lock_flags = shared_lock.lock_irqsave();
        if (cpu::interrupts_enabled()) {
            __atomic_store_n(&lock_if_fault, true, __ATOMIC_RELEASE);
        }
        if (__atomic_add_fetch(&critical_active, 1, __ATOMIC_ACQ_REL) != 1) {
            __atomic_store_n(&critical_overlap_fault, true, __ATOMIC_RELEASE);
        }
        ++shared_counter;
        for (uint32_t delay = 0; delay < 8; ++delay) {
            cpu::relax();
        }
        __atomic_sub_fetch(&critical_active, 1, __ATOMIC_ACQ_REL);
        __atomic_store_n(&progress[id], iteration + 1, __ATOMIC_RELEASE);
        shared_lock.unlock_irqrestore(lock_flags);
        if (!cpu::interrupts_enabled()) {
            __atomic_store_n(&lock_if_fault, true, __ATOMIC_RELEASE);
            set_worker_fault();
            return;
        }

        // This is the current CPU's tick, not a global BSP counter. Every
        // worker must cross its own LAPIC interrupt before continuing.
        const uint64_t observed_tick = scheduler::ticks();
        while (observed_tick == scheduler::ticks()) {
            if (!scheduler::started()) {
                set_worker_fault();
                return;
            }
            cpu::wait_for_interrupt();
        }

        if ((iteration & 7) == 7) {
            const uint64_t before_sleep = scheduler::ticks();
            if (!scheduler::sleep(2)) {
                __atomic_store_n(&sleep_fault, true, __ATOMIC_RELEASE);
                set_worker_fault();
                return;
            }
            const uint64_t slept = scheduler::ticks() - before_sleep;
            if (slept < 2) {
                __atomic_store_n(&sleep_fault, true, __ATOMIC_RELEASE);
                set_worker_fault();
                return;
            }
            __atomic_add_fetch(&sleep_calls, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&observed_sleep_ticks, slept, __ATOMIC_RELAXED);
        }
    }
    if (!exercise_memory_allocators(id) || !exercise_tlb_shootdown(id)) {
        return;
    }
    __atomic_store_n(&observed_cpu[id], running_cpu + 1, __ATOMIC_RELEASE);
}

void test_spinlock() {
    SpinLock lock;
    require(!cpu::interrupts_enabled(),
            "runtime tests must begin with interrupts disabled");
    const uint64_t flags = lock.lock_irqsave();
    require(!cpu::interrupts_enabled(), "IRQ-safe lock left IF enabled");
    require(!lock.try_lock(), "spinlock try_lock succeeded while held");
    lock.unlock_irqrestore(flags);
    require(!cpu::interrupts_enabled(), "IRQ lock incorrectly enabled IF");
    require(lock.try_lock(), "spinlock was not released by its guard");
    lock.unlock();
}

uint32_t popcount32(uint32_t value) {
    uint32_t count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

}  // namespace

void run() {
    test_spinlock();
    scheduler::initialize();
    const size_t cpu_count = scheduler::configured_processor_count();
    require(cpu_count > 0 && cpu_count <= smp::maximum_processors,
            "invalid scheduler CPU count");
    expected_workers = static_cast<uint32_t>(cpu_count + 1);

    shared_counter = 0;
    __atomic_store_n(&worker_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&affinity_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&lock_if_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&critical_overlap_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&sleep_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&migration_test_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&memory_test_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&memory_allocated, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&memory_verified, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&memory_released, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&memory_cpu_mask, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&runtime_pmm_allocations, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&runtime_heap_allocations, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&tlb_ready, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&tlb_probed, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&tlb_phase, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&probe_cpu_mask, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&probe_write_mask, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&probe_faults, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&tlb_test_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&recycle_progress, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&recycle_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&event_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&event_wait_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&event_signal_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&queue_wait_ready, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&queue_wait_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&queue_broadcast_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex_critical, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&mutex_enter_ready, 0, __ATOMIC_RELAXED);
    mutex_shared_counter = 0;
    __atomic_store_n(&socket_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&socket_wait_ready, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&socket_wait_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&socket_deliver_done, 0, __ATOMIC_RELAXED);
    net::udp_sockets_init(socket_table);
    socket_slot = net::udp_bind(socket_table, socket_test_port);
    require(socket_slot >= 0, "cannot bind M22 UDP socket test port");
    scheduler::event_init(lifecycle_event);
    scheduler::event_init(queue_event);
    scheduler::mutex_init(lifecycle_mutex);
    __atomic_store_n(&critical_active, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sleep_calls, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&observed_sleep_ticks, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&arrived_workers, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&arrival_cpu_mask, 0, __ATOMIC_RELAXED);
    for (size_t index = 0; index < maximum_workers; ++index) {
        __atomic_store_n(&progress[index], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&observed_cpu[index], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&visited_cpu_mask[index], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&memory_pages[index], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&memory_blocks[index], 0, __ATOMIC_RELAXED);
    }

    require(expected_workers * sizeof(uint64_t) < pmm::page_size,
            "TLB probe page is too small for worker slots");
    const uint64_t test_physical = pmm::allocate_pages(1);
    require(test_physical != 0 &&
                vmm::map_kernel_stack(test_physical, test_page_base, 1),
            "cannot allocate/map the runtime TLB probe page");
    require(!vmm::is_mapped(test_page_base - pmm::page_size),
            "TLB probe page guard is mapped");
    const vmm::MappingInfo initial_probe_mapping =
        vmm::inspect_mapping(test_page_base);
    require(initial_probe_mapping.present && initial_probe_mapping.writable &&
                !initial_probe_mapping.executable,
            "TLB probe page is not initially RW/NX");
    auto* const probe_page = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(test_page_base));
    for (size_t id = 0; id < expected_workers; ++id) {
        probe_page[id] = tlb_pattern(id, 0);
    }

    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        const size_t workers = cpu_index == 0 ? 2 : 1;
        for (size_t worker = 0; worker < workers; ++worker) {
            const size_t id = cpu_index == 0 ? worker : cpu_index + 1;
            worker_arguments[id] = WorkerArgument{
                id, static_cast<uint32_t>(cpu_index), 0};
            require(scheduler::create_thread_on_cpu(
                        static_cast<uint32_t>(cpu_index), worker_entry,
                        &worker_arguments[id]),
                    "cannot create per-CPU scheduler test thread");
            const uint64_t base = scheduler::worker_stack_base(
                static_cast<uint32_t>(cpu_index), worker);
            require(!vmm::is_mapped(base - 4096),
                    "per-CPU worker stack guard is mapped");
            const vmm::MappingInfo stack = vmm::inspect_mapping(base);
            require(stack.present && stack.writable && !stack.executable,
                    "per-CPU worker stack is not RW/NX");
        }
    }

    const uint64_t pmm_before_runtime = pmm::free_pages_count();
    const uint64_t heap_before_runtime = heap::reserved_pages();
    const uint64_t first_tick = scheduler::ticks();
    require(scheduler::start(), "cannot start per-CPU schedulers");
    bool queue_broadcast_sent = false;
    while (scheduler::finished_threads() < expected_workers) {
        if (__atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) == 0 &&
            __atomic_load_n(&tlb_ready, __ATOMIC_ACQUIRE) == expected_workers) {
            if (!vmm::set_page_writable(test_page_base, false)) {
                log::panic("read-only runtime PTE/TLB shootdown failed");
            }
            __atomic_store_n(&tlb_phase, 1, __ATOMIC_RELEASE);
        } else if (__atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) == 1 &&
                   __atomic_load_n(&tlb_probed, __ATOMIC_ACQUIRE) ==
                       expected_workers) {
            if (!vmm::set_page_writable(test_page_base, true)) {
                log::panic("writable runtime PTE/TLB shootdown failed");
            }
            __atomic_store_n(&tlb_phase, 2, __ATOMIC_RELEASE);
        }
        if (!queue_broadcast_sent &&
            __atomic_load_n(&queue_wait_ready, __ATOMIC_ACQUIRE) >= 2) {
            // ready is posted before wait() parks; wait a few ticks.
            const uint64_t mark = scheduler::ticks();
            while (scheduler::ticks() - mark < 4) {
                cpu::wait_for_interrupt();
            }
            require(scheduler::broadcast(queue_event),
                    "BSP broadcast of queue_event failed");
            __atomic_store_n(&queue_broadcast_done, 1, __ATOMIC_RELEASE);
            queue_broadcast_sent = true;
        }
        if (__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE)) {
            log::panic("per-CPU scheduler worker detected corrupted state");
        }
        if (scheduler::ticks() - first_tick > 5000) {
            log::panic("per-CPU scheduler runtime test timed out");
        }
        cpu::wait_for_interrupt();
    }

    // M15: Finished slots must reclaim to Empty (thread_total back to 1).
    while (scheduler::reclaimed_threads() < expected_workers) {
        if (scheduler::ticks() - first_tick > 5000) {
            log::panic("Finished-slot reclaim timed out");
        }
        cpu::wait_for_interrupt();
    }
    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        require(scheduler::thread_count_for_cpu(
                    static_cast<uint32_t>(cpu_index)) == 1,
                "Finished worker slot was not reclaimed to Empty");
    }

    // M19: return wave-1 stacks to PMM, then recreate with fresh maps.
    const uint64_t pmm_before_release = pmm::free_pages_count();
    const uint64_t shootdowns_before_release = vmm::tlb_shootdown_count();
    require(scheduler::release_empty_stacks(),
            "cannot release reclaimed worker stacks");
    require(scheduler::released_stack_pages() ==
                    expected_workers * scheduler::thread_stack_pages &&
                pmm::free_pages_count() ==
                    pmm_before_release +
                        expected_workers * scheduler::thread_stack_pages &&
                vmm::tlb_shootdown_count() ==
                    shootdowns_before_release + expected_workers,
            "M19 stack release PMM/TLB contract failed");
    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        const size_t workers = cpu_index == 0 ? 2 : 1;
        for (size_t worker = 0; worker < workers; ++worker) {
            require(!vmm::is_mapped(scheduler::worker_stack_base(
                        static_cast<uint32_t>(cpu_index), worker)),
                    "released worker stack is still mapped");
        }
    }

    const uint64_t finished_before_recycle = scheduler::finished_threads();
    const uint64_t reclaimed_before_recycle = scheduler::reclaimed_threads();
    const uint64_t pmm_before_recycle = pmm::free_pages_count();
    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        recycle_arguments[cpu_index] = WorkerArgument{
            cpu_index, static_cast<uint32_t>(cpu_index), 0};
        require(scheduler::create_thread_on_cpu(
                    static_cast<uint32_t>(cpu_index), recycle_worker_entry,
                    &recycle_arguments[cpu_index]),
                "cannot recreate worker after stack release");
        const uint64_t base = scheduler::worker_stack_base(
            static_cast<uint32_t>(cpu_index), 0);
        require(vmm::is_mapped(base) && !vmm::is_mapped(base - 4096),
                "recreated worker stack mapping invalid");
    }
    require(pmm::free_pages_count() ==
                pmm_before_recycle -
                    cpu_count * scheduler::thread_stack_pages,
            "recycle create did not consume stack pages");
    while (scheduler::finished_threads() <
           finished_before_recycle + cpu_count) {
        if (__atomic_load_n(&recycle_fault, __ATOMIC_ACQUIRE)) {
            log::panic("recycle worker affinity fault");
        }
        if (scheduler::ticks() - first_tick > 5000) {
            log::panic("recycle worker finish timed out");
        }
        cpu::wait_for_interrupt();
    }
    while (scheduler::reclaimed_threads() <
           reclaimed_before_recycle + cpu_count) {
        if (scheduler::ticks() - first_tick > 5000) {
            log::panic("recycle worker reclaim timed out");
        }
        cpu::wait_for_interrupt();
    }
    require(__atomic_load_n(&recycle_progress, __ATOMIC_ACQUIRE) == cpu_count &&
                !__atomic_load_n(&recycle_fault, __ATOMIC_ACQUIRE),
            "recycle worker progress mismatch");
    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        require(scheduler::thread_count_for_cpu(
                    static_cast<uint32_t>(cpu_index)) == 1,
                "recycle Finished slot was not reclaimed");
    }
    const uint64_t pages_before_second_release =
        scheduler::released_stack_pages();
    const uint64_t shootdowns_before_second = vmm::tlb_shootdown_count();
    require(scheduler::release_empty_stacks(),
            "cannot release recycle worker stacks");
    require(scheduler::released_stack_pages() ==
                    pages_before_second_release +
                        cpu_count * scheduler::thread_stack_pages &&
                vmm::tlb_shootdown_count() ==
                    shootdowns_before_second + cpu_count &&
                pmm::free_pages_count() == pmm_before_recycle,
            "M19 second stack release did not restore PMM");

    const uint64_t elapsed_ticks = scheduler::ticks_for_cpu(0) - first_tick;
    require(scheduler::stop(), "cannot stop and drain per-CPU schedulers");
    require(!cpu::interrupts_enabled(),
            "scheduler stop did not disable interrupts");

    require(!__atomic_load_n(&worker_fault, __ATOMIC_ACQUIRE),
            "per-CPU scheduler worker failed");
    require(!__atomic_load_n(&affinity_fault, __ATOMIC_ACQUIRE),
            "worker executed on the wrong CPU");
    require(!__atomic_load_n(&lock_if_fault, __ATOMIC_ACQUIRE),
            "IRQ-safe lock did not preserve interrupt state");
    require(!__atomic_load_n(&critical_overlap_fault, __ATOMIC_ACQUIRE) &&
                __atomic_load_n(&critical_active, __ATOMIC_ACQUIRE) == 0,
            "SMP shared spinlock allowed overlapping critical sections");
    require(!__atomic_load_n(&sleep_fault, __ATOMIC_ACQUIRE),
            "per-CPU timer sleep/wake failed");
    require(!__atomic_load_n(&event_fault, __ATOMIC_ACQUIRE) &&
                __atomic_load_n(&event_wait_done, __ATOMIC_ACQUIRE) == 1 &&
                __atomic_load_n(&event_signal_done, __ATOMIC_ACQUIRE) == 1 &&
                __atomic_load_n(&queue_wait_done, __ATOMIC_ACQUIRE) == 2 &&
                __atomic_load_n(&queue_broadcast_done, __ATOMIC_ACQUIRE) == 1 &&
                scheduler::event_wait_count() == 4 &&
                scheduler::event_signal_count() == 2 &&
                scheduler::event_broadcast_count() == 1 &&
                scheduler::event_wake_count() == 4,
            "M16/M17/M22 event wait/signal/broadcast contract failed");
    require(!__atomic_load_n(&mutex_fault, __ATOMIC_ACQUIRE) &&
                mutex_shared_counter ==
                    mutex_rounds * expected_workers &&
                __atomic_load_n(&mutex_critical, __ATOMIC_ACQUIRE) == 0 &&
                scheduler::mutex_lock_count() ==
                    mutex_rounds * expected_workers &&
                scheduler::mutex_unlock_count() ==
                    mutex_rounds * expected_workers &&
                scheduler::mutex_wait_count() > 0,
            "M18 mutex exclusion/accounting contract failed");
    require(!__atomic_load_n(&socket_fault, __ATOMIC_ACQUIRE) &&
                __atomic_load_n(&socket_wait_done, __ATOMIC_ACQUIRE) == 1 &&
                __atomic_load_n(&socket_deliver_done, __ATOMIC_ACQUIRE) == 1,
            "M22 UDP socket blocking recv contract failed");
    require(net::udp_socket_contract_ok(),
            "M21/M23 UDP socket bind/deliver/recv/send contract failed");
    require(!__atomic_load_n(&migration_test_fault, __ATOMIC_ACQUIRE) &&
                !scheduler::migration_failed(),
            "controlled thread migration failed");
    require(scheduler::migration_count() == (cpu_count > 1 ? 2 : 0),
            "completed thread migration count mismatch");
    if (cpu_count > 1) {
        require(scheduler::migration_wakeup_ipi_count(0) == 1 &&
                    scheduler::migration_wakeup_ipi_count(1) == 1,
                "cross-CPU migration wakeup IPI mismatch");
    }
    require(!__atomic_load_n(&memory_test_fault, __ATOMIC_ACQUIRE),
            "concurrent PMM/heap content or ownership check failed");
    require(__atomic_load_n(&memory_allocated, __ATOMIC_ACQUIRE) ==
                    expected_workers * memory_rounds &&
                __atomic_load_n(&memory_verified, __ATOMIC_ACQUIRE) ==
                    expected_workers * memory_rounds &&
                __atomic_load_n(&memory_released, __ATOMIC_ACQUIRE) ==
                    expected_workers * memory_rounds &&
                __atomic_load_n(&runtime_pmm_allocations, __ATOMIC_ACQUIRE) ==
                    expected_workers * memory_rounds &&
                __atomic_load_n(&runtime_heap_allocations, __ATOMIC_ACQUIRE) ==
                    expected_workers * memory_rounds,
            "concurrent PMM/heap operation count mismatch");
    const uint64_t heap_after_runtime = heap::reserved_pages();
    // Wave-1 worker stacks were returned (M19); recycle stacks also returned.
    require(heap_after_runtime >= heap_before_runtime &&
                pmm::free_pages_count() ==
                    pmm_before_runtime +
                        expected_workers * scheduler::thread_stack_pages -
                        (heap_after_runtime - heap_before_runtime),
            "concurrent PMM/heap accounting did not close");

    const uint32_t expected_mask =
        (uint32_t{1} << static_cast<uint32_t>(cpu_count)) - 1;
    require(__atomic_load_n(&arrived_workers, __ATOMIC_ACQUIRE) ==
                expected_workers &&
                __atomic_load_n(&arrival_cpu_mask, __ATOMIC_ACQUIRE) ==
                    expected_mask,
            "not every CPU reached the live-worker barrier");
    require(__atomic_load_n(&memory_cpu_mask, __ATOMIC_ACQUIRE) == expected_mask,
            "not every CPU exercised the SMP-safe memory allocators");
    require(!__atomic_load_n(&tlb_test_fault, __ATOMIC_ACQUIRE) &&
                !vmm::tlb_shootdown_failed() &&
                __atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) == 2,
            "runtime read-only probe or TLB test failed");
    const uint64_t expected_shootdowns =
        2 + expected_workers + cpu_count;  // probe + two stack-release waves
    require(vmm::tlb_shootdown_count() == expected_shootdowns &&
                __atomic_load_n(&probe_faults, __ATOMIC_ACQUIRE) ==
                    expected_workers &&
                __atomic_load_n(&probe_cpu_mask, __ATOMIC_ACQUIRE) ==
                    expected_mask &&
                __atomic_load_n(&probe_write_mask, __ATOMIC_ACQUIRE) ==
                    expected_mask,
            "TLB shootdown/fault CPU coverage mismatch");
    const vmm::MappingInfo final_probe_mapping =
        vmm::inspect_mapping(test_page_base);
    require(final_probe_mapping.present && final_probe_mapping.writable &&
                !final_probe_mapping.executable,
            "TLB probe page was not restored to RW/NX");
    require(__atomic_load_n(&sleep_calls, __ATOMIC_ACQUIRE) ==
                    4ull * expected_workers &&
                __atomic_load_n(&observed_sleep_ticks, __ATOMIC_ACQUIRE) >=
                    8ull * expected_workers,
            "per-CPU sleep/wake count or duration mismatch");
    require(shared_counter == worker_iterations * expected_workers,
            "SMP shared counter lost an update");
    for (size_t id = 0; id < expected_workers; ++id) {
        const uint32_t owner_mask =
            uint32_t{1} << worker_arguments[id].cpu_index;
        const uint32_t expected_visited =
            id == 1 && cpu_count > 1 ? uint32_t{3} : owner_mask;
        require(__atomic_load_n(&progress[id], __ATOMIC_ACQUIRE) ==
                    worker_iterations &&
                    __atomic_load_n(&observed_cpu[id], __ATOMIC_ACQUIRE) ==
                        worker_arguments[id].cpu_index + 1 &&
                    __atomic_load_n(&visited_cpu_mask[id], __ATOMIC_ACQUIRE) ==
                        expected_visited,
                "worker affinity or controlled round-trip migration mismatch");
    }

    for (size_t cpu_index = 0; cpu_index < cpu_count; ++cpu_index) {
        // Wave-1 workers + one recycle worker per CPU; slots reclaimed to 1.
        const uint64_t expected_finished =
            (cpu_index == 0 ? 2 : 1) + 1;
        require(scheduler::thread_count_for_cpu(
                    static_cast<uint32_t>(cpu_index)) == 1 &&
                    scheduler::finished_threads_for_cpu(
                        static_cast<uint32_t>(cpu_index)) == expected_finished &&
                    scheduler::reclaimed_threads_for_cpu(
                        static_cast<uint32_t>(cpu_index)) == expected_finished,
                "per-CPU run queue completion/reclaim mismatch");
        require(scheduler::ticks_for_cpu(static_cast<uint32_t>(cpu_index)) >= 8 &&
                    scheduler::context_switches_for_cpu(
                        static_cast<uint32_t>(cpu_index)) >= 8,
                "a CPU did not perform local preemption/sleep scheduling");
        if (vmm::tlb_invalidations_for_cpu(
                static_cast<uint32_t>(cpu_index)) != expected_shootdowns) {
            log::write("[debug] TLB invalidations cpu ");
            log::decimal(cpu_index);
            log::write(": ");
            log::decimal(vmm::tlb_invalidations_for_cpu(
                static_cast<uint32_t>(cpu_index)));
            log::write(" expected ");
            log::decimal(expected_shootdowns);
            log::write('\n');
            log::panic("a CPU missed a runtime TLB invalidation");
        }
        if (cpu_index != 0) {
            require(smp::scheduler_control_ipi_count(
                        static_cast<uint32_t>(cpu_index)) == 2,
                    "AP scheduler start/stop control IPI mismatch");
        }
        log::write("[ok] CPU ");
        log::decimal(cpu_index);
        log::write(" scheduler ticks: ");
        log::decimal(scheduler::ticks_for_cpu(
            static_cast<uint32_t>(cpu_index)));
        log::write(", switches: ");
        log::decimal(scheduler::context_switches_for_cpu(
            static_cast<uint32_t>(cpu_index)));
        log::write(", finished/reclaimed: ");
        log::decimal(scheduler::finished_threads_for_cpu(
            static_cast<uint32_t>(cpu_index)));
        log::write('/');
        log::decimal(scheduler::reclaimed_threads_for_cpu(
            static_cast<uint32_t>(cpu_index)));
        log::write('\n');
    }

    log::write("[ok] live-worker CPUs: ");
    log::decimal(popcount32(__atomic_load_n(&arrival_cpu_mask,
                                           __ATOMIC_ACQUIRE)));
    log::write(", workers: ");
    log::decimal(expected_workers);
    log::write(", shared counter: ");
    log::decimal(shared_counter);
    log::write(", sleep/wake cycles: ");
    log::decimal(__atomic_load_n(&sleep_calls, __ATOMIC_ACQUIRE));
    log::write('\n');
    log::write("[ok] event wait/signal/broadcast/wake: ");
    log::decimal(scheduler::event_wait_count());
    log::write('/');
    log::decimal(scheduler::event_signal_count());
    log::write('/');
    log::decimal(scheduler::event_broadcast_count());
    log::write('/');
    log::decimal(scheduler::event_wake_count());
    log::write('\n');
    log::write("[ok] mutex lock/unlock/wait: ");
    log::decimal(scheduler::mutex_lock_count());
    log::write('/');
    log::decimal(scheduler::mutex_unlock_count());
    log::write('/');
    log::decimal(scheduler::mutex_wait_count());
    log::write(", shared: ");
    log::decimal(mutex_shared_counter);
    log::write('\n');
    log::write("[ok] released stack pages: ");
    log::decimal(scheduler::released_stack_pages());
    log::write(", TLB shootdowns: ");
    log::decimal(vmm::tlb_shootdown_count());
    log::write('\n');
    log::line("[ok] UDP socket bind/deliver/recv/send contract");
    log::line("[ok] UDP socket blocking recv/wake contract");
    log::write("[ok] BSP local APIC timer ticks: ");
    log::decimal(elapsed_ticks);
    log::write(", spurious IRQs: ");
    log::decimal(apic::spurious_interrupts());
    log::write(", accumulated slept ticks: ");
    log::decimal(__atomic_load_n(&observed_sleep_ticks, __ATOMIC_ACQUIRE));
    log::write('\n');
    log::write("[ok] SMP PMM/heap rounds: ");
    log::decimal(memory_rounds);
    log::write(", allocations PMM/heap: ");
    log::decimal(__atomic_load_n(&runtime_pmm_allocations, __ATOMIC_ACQUIRE));
    log::write('/');
    log::decimal(__atomic_load_n(&runtime_heap_allocations, __ATOMIC_ACQUIRE));
    log::write(", heap page delta: ");
    log::decimal(heap_after_runtime - heap_before_runtime);
    log::write('\n');
    log::write("[ok] runtime TLB shootdowns: ");
    log::decimal(vmm::tlb_shootdown_count());
    log::write(", recovered write faults: ");
    log::decimal(__atomic_load_n(&probe_faults, __ATOMIC_ACQUIRE));
    log::write('\n');
    log::write("[ok] completed cross-CPU migrations: ");
    log::decimal(scheduler::migration_count());
    log::write(", wake IPIs BSP/CPU1: ");
    log::decimal(scheduler::migration_wakeup_ipi_count(0));
    log::write('/');
    log::decimal(cpu_count > 1 ? scheduler::migration_wakeup_ipi_count(1) : 0);
    log::write('\n');
    log::line("[ok] SMP spinlock, per-CPU preemption, and timer sleep/wake");
}

bool on_page_fault(cpu::InterruptFrame& frame) {
    if (__atomic_load_n(&tlb_phase, __ATOMIC_ACQUIRE) != 1 ||
        frame.rip != reinterpret_cast<uint64_t>(&runtime_write_probe_instruction) ||
        (frame.error_code & 0x1f) != 0x03) {
        return false;
    }
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    if (cr2 < test_page_base || cr2 >= test_page_base + pmm::page_size) {
        return false;
    }
    const uint32_t cpu_index = smp::current_cpu_index();
    if (cpu_index >= smp::maximum_processors) {
        return false;
    }
    __atomic_or_fetch(&probe_cpu_mask, uint32_t{1} << cpu_index,
                      __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&probe_faults, 1, __ATOMIC_RELAXED);
    frame.rip = reinterpret_cast<uint64_t>(&runtime_write_probe_recovery);
    return true;
}

}  // namespace cxc::runtime_test
