#pragma once

#include <cxc/cpu.hpp>
#include <cxc/smp.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::scheduler {

using ThreadEntry = void (*)(void*);

// Per-CPU fixed run queues: bootstrap plus at most two workers. M10 permits a
// running worker to migrate into an empty target slot; slots are never
// compacted while occupied. M15 reclaims Finished workers to Empty and may
// reuse the same (cpu, slot) stack mapping; bootstrap is never destroyed.
// M16–M18: Event queue and worker Mutex. M19: Empty slots may release their
// stack pages (BSP unmap + PMM free); recreate then allocate/maps again.
constexpr size_t maximum_threads = 3;
constexpr size_t workers_per_cpu = maximum_threads - 1;
// Must cover all runtime-test workers (cpu_count+1) contending on one Event /
// Mutex gate — previously 8, which panics once SMP exceeds ~8 CPUs.
constexpr size_t event_waiter_capacity = smp::maximum_processors + 1;
constexpr uint64_t thread_stack_pages = 8;
constexpr uint64_t worker_stack_region = 0xffffffff70000000;
constexpr uint64_t worker_stack_stride = 0x200000;

constexpr uint64_t worker_stack_base(uint32_t cpu_index, size_t worker_index) {
    return worker_stack_region +
           (static_cast<uint64_t>(cpu_index) * workers_per_cpu + worker_index) *
               worker_stack_stride;
}

void initialize();
bool create_thread_on_cpu(uint32_t cpu_index, ThreadEntry entry, void* argument);
bool create_thread(ThreadEntry entry, void* argument);  // BSP compatibility.
// BSP-only: unmap+free stacks cached on Empty worker slots (M19).
bool release_empty_stacks();
uint64_t released_stack_pages();
bool start();
bool stop();

// AP park-loop hooks. The control IPI only wakes the loop; it never runs the
// scheduler in interrupt context. application_processor_main returns after
// the BSP stops all per-CPU schedulers.
bool application_processors_should_start();
void application_processor_main();

// Timer-based sleep for worker threads on the current CPU. Bootstrap remains
// the non-sleeping fallback on every run queue.
bool sleep(uint64_t ticks);

// Sticky Event with a bounded FIFO waiter queue. wait() consumes one sticky
// signal or enqueues; signal() wakes the head waiter or posts sticky;
// broadcast() wakes every waiter (or posts one sticky if the queue is empty).
// Cross-CPU wake uses vector 50. Not a mutex/condvar.
struct Event {
    uint32_t signaled;
    uint32_t waiter_count;
    uint32_t waiter_cpu[event_waiter_capacity];
    uint32_t waiter_slot[event_waiter_capacity];
};

void event_init(Event& event);
bool wait(Event& event);
bool signal(Event& event);
bool broadcast(Event& event);
// Consume one sticky signal without enqueueing. Safe before scheduler start
// (and from BSP bootstrap). Returns false if no sticky is posted.
bool event_try_consume(Event& event);

// M18: blocking mutex for workers. Contended lock enqueues on an internal
// Event waiter list (no sticky). Not recursive; bootstrap may not lock.
struct Mutex {
    uint32_t locked;
    uint32_t owner_cpu;
    uint32_t owner_slot;
    Event gate;
};

void mutex_init(Mutex& mutex);
bool mutex_lock(Mutex& mutex);
bool mutex_unlock(Mutex& mutex);

// Cooperatively initiated, interrupt-frame-preserving migration. The source
// timer saves the frame, switches away, and only a later source tick publishes
// it to the target. The same invocation resumes on the target CPU.
bool migrate_to(uint32_t target_cpu);

// Called by vector 32. Run queues remain per CPU; a correctness-first global
// IRQ-safe queue lock serializes state changes and two-phase migration.
cpu::InterruptFrame* on_timer(cpu::InterruptFrame* frame);

// Vector 50 only records/wakes a target with newly migrated work; the periodic
// local timer performs the actual selection in ordinary interrupt context.
void on_wakeup_ipi();

// Current-CPU views (or the early BSP tick counter before initialize).
uint64_t ticks();
uint64_t context_switches();
size_t thread_count();
size_t current_thread();
uint64_t current_thread_stack_base();
bool started();

// Cross-CPU completion and read-only snapshots after/during the runtime test.
uint64_t finished_threads();
uint64_t reclaimed_threads();
uint64_t event_wait_count();
uint64_t event_signal_count();
uint64_t event_broadcast_count();
uint64_t event_wake_count();
uint64_t mutex_lock_count();
uint64_t mutex_unlock_count();
uint64_t mutex_wait_count();
uint64_t ticks_for_cpu(uint32_t cpu_index);
uint64_t context_switches_for_cpu(uint32_t cpu_index);
uint64_t finished_threads_for_cpu(uint32_t cpu_index);
uint64_t reclaimed_threads_for_cpu(uint32_t cpu_index);
size_t thread_count_for_cpu(uint32_t cpu_index);
size_t configured_processor_count();

// Completed ownership transfers, not merely requests or IPI counts.
uint64_t migration_count();
uint64_t migration_wakeup_ipi_count(uint32_t cpu_index);
bool migration_failed();

}  // namespace cxc::scheduler
