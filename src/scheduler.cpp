#include <cxc/scheduler.hpp>

#include <cxc/apic.hpp>
#include <cxc/log.hpp>
#include <cxc/pmm.hpp>
#include <cxc/smp.hpp>
#include <cxc/spinlock.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void*, int, size_t);

namespace cxc::scheduler {
namespace {

enum class ThreadState : uint32_t {
    Empty,
    Ready,
    Running,
    Sleeping,
    Waiting,
    Finished,
    MigrationPending,
    MigrationSaved,
};

struct Thread {
    cpu::InterruptFrame* saved_frame;
    ThreadEntry entry;
    void* argument;
    uint64_t stack_base;
    uint64_t stack_physical;
    uint64_t wake_tick;
    ThreadState state;
    uint32_t target_cpu;
    uint32_t reserved;
};

struct alignas(64) PerCpuScheduler {
    // Slot zero is the non-migrating bootstrap. Occupied worker slots are not
    // compacted (Sleeping/Ready addresses stay stable). Finished workers are
    // reclaimed to Empty and may keep their mapped stack for reuse.
    Thread threads[maximum_threads];
    size_t thread_total;
    size_t current_index;
    volatile uint64_t tick_count;
    volatile uint64_t switch_count;
    volatile uint64_t finished_count;
    volatile uint64_t reclaimed_count;
    volatile uint64_t wakeup_ipi_count;
    volatile bool started;
};

PerCpuScheduler processors[smp::maximum_processors];
size_t configured_cpus = 0;
volatile uint64_t early_tick_count = 0;
volatile bool control_start = false;
volatile uint32_t application_running_count = 0;
volatile uint64_t completed_migrations = 0;
volatile bool migration_active = false;
volatile bool migration_fault = false;
volatile uint64_t event_waits = 0;
volatile uint64_t event_signals = 0;
volatile uint64_t event_broadcasts = 0;
volatile uint64_t event_wakes = 0;
volatile uint64_t mutex_locks = 0;
volatile uint64_t mutex_unlocks = 0;
volatile uint64_t mutex_waits = 0;
volatile uint64_t released_stack_page_count = 0;
SpinLock queue_lock;

PerCpuScheduler* current_processor() {
    if (configured_cpus == 0) {
        return nullptr;
    }
    const uint32_t index = smp::current_cpu_index();
    return index < configured_cpus ? &processors[index] : nullptr;
}

// Called with queue_lock held. Leaves stack_base/physical for reuse; clears
// the runnable identity so the slot is Empty for migration or recreate.
void reclaim_finished_slots(PerCpuScheduler& self) {
    for (size_t slot = 1; slot < maximum_threads; ++slot) {
        if (self.current_index == slot) {
            continue;
        }
        Thread& candidate = self.threads[slot];
        if (candidate.state != ThreadState::Finished) {
            continue;
        }
        const uint64_t stack_base = candidate.stack_base;
        const uint64_t stack_physical = candidate.stack_physical;
        candidate = Thread{};
        candidate.stack_base = stack_base;
        candidate.stack_physical = stack_physical;
        candidate.state = ThreadState::Empty;
        candidate.target_cpu = UINT32_MAX;
        if (self.thread_total > 1) {
            --self.thread_total;
        }
        __atomic_add_fetch(&self.reclaimed_count, 1, __ATOMIC_RELEASE);
    }
}

[[noreturn]] void thread_entry();

[[noreturn]] void finish_current_thread() {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr) {
        cpu::halt();
    }
    const uint64_t flags = queue_lock.lock_irqsave();
    self->threads[self->current_index].state = ThreadState::Finished;
    __atomic_add_fetch(&self->finished_count, 1, __ATOMIC_RELEASE);
    queue_lock.unlock_irqrestore(flags);

    // Marked Finished: local timer switches to bootstrap/Ready, then a later
    // tick reclaims this slot to Empty (stack mapping retained for reuse).
    cpu::enable_interrupts();
    for (;;) {
        cpu::wait_for_interrupt();
    }
}

void prepare_thread_frame(Thread& thread, ThreadEntry entry, void* argument) {
    auto* stack_memory =
        reinterpret_cast<void*>(static_cast<uintptr_t>(thread.stack_base));
    memset(stack_memory, 0, thread_stack_pages * pmm::page_size);

    const uint64_t stack_top =
        thread.stack_base + thread_stack_pages * pmm::page_size;
    const uint64_t initial_rsp = stack_top - 8;
    auto* frame = reinterpret_cast<cpu::InterruptFrame*>(
        static_cast<uintptr_t>(initial_rsp - sizeof(cpu::InterruptFrame)));
    *frame = cpu::InterruptFrame{};
    frame->vector = cpu::timer_vector;
    frame->rip = reinterpret_cast<uint64_t>(&thread_entry);
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = initial_rsp;
    frame->ss = 0x10;
    *reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(initial_rsp)) = 0;

    thread.saved_frame = frame;
    thread.entry = entry;
    thread.argument = argument;
    thread.wake_tick = 0;
    thread.state = ThreadState::Ready;
    thread.target_cpu = UINT32_MAX;
}

[[noreturn]] void thread_entry() {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr) {
        cpu::halt();
    }
    const Thread& current = self->threads[self->current_index];
    const ThreadEntry entry = current.entry;
    void* const argument = current.argument;
    if (entry == nullptr) {
        cpu::halt();
    }
    entry(argument);
    finish_current_thread();
}

bool wait_for_application_count(uint32_t expected) {
    for (uint32_t guard = 0; guard < 100000000; ++guard) {
        if (__atomic_load_n(&application_running_count, __ATOMIC_ACQUIRE) ==
            expected) {
            return true;
        }
        cpu::relax();
    }
    return false;
}

// Called with queue_lock held at the beginning of a later source-CPU tick.
// A MigrationSaved frame cannot be published in the interrupt that saved it:
// the old CPU has not executed iretq yet and the two CPUs could otherwise use
// one kernel stack concurrently.
void publish_saved_migrations(PerCpuScheduler& source, uint32_t source_cpu,
                              uint32_t* wake_targets, size_t& wake_count) {
    for (size_t slot = 1; slot < maximum_threads; ++slot) {
        Thread& candidate = source.threads[slot];
        if (candidate.state != ThreadState::MigrationSaved) {
            continue;
        }
        const uint32_t target_cpu = candidate.target_cpu;
        if (target_cpu >= configured_cpus || target_cpu == source_cpu ||
            source.current_index == slot ||
            !__atomic_load_n(&processors[target_cpu].started,
                             __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&migration_fault, true, __ATOMIC_RELEASE);
            continue;
        }

        PerCpuScheduler& target = processors[target_cpu];
        size_t target_slot = maximum_threads;
        for (size_t index = 1; index < maximum_threads; ++index) {
            if (target.threads[index].state == ThreadState::Empty) {
                target_slot = index;
                break;
            }
        }
        if (target_slot == maximum_threads || target.thread_total >= maximum_threads) {
            __atomic_store_n(&migration_fault, true, __ATOMIC_RELEASE);
            continue;
        }

        Thread moving = candidate;
        moving.state = ThreadState::Ready;
        moving.target_cpu = UINT32_MAX;
        target.threads[target_slot] = moving;
        ++target.thread_total;
        candidate = Thread{};
        --source.thread_total;
        __atomic_add_fetch(&completed_migrations, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&migration_active, false, __ATOMIC_RELEASE);
        wake_targets[wake_count++] = target_cpu;
    }
}

}  // namespace

void initialize() {
    if (cpu::interrupts_enabled()) {
        log::panic("scheduler initialization with interrupts enabled");
    }
    if (smp::current_cpu_index() != 0) {
        log::panic("scheduler initialization must run on the BSP");
    }

    configured_cpus = smp::online_processor_count();
    if (configured_cpus == 0 || configured_cpus > smp::maximum_processors) {
        log::panic("invalid online processor count for scheduler");
    }
    __atomic_store_n(&control_start, false, __ATOMIC_RELEASE);
    __atomic_store_n(&application_running_count, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&completed_migrations, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&migration_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&migration_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&event_waits, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&event_signals, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&event_broadcasts, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&event_wakes, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex_locks, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex_unlocks, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex_waits, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&released_stack_page_count, 0, __ATOMIC_RELAXED);
    queue_lock.unlock();

    for (size_t cpu_index = 0; cpu_index < configured_cpus; ++cpu_index) {
        PerCpuScheduler& self = processors[cpu_index];
        for (size_t index = 0; index < maximum_threads; ++index) {
            self.threads[index] = Thread{};
        }
        self.threads[0].state = ThreadState::Running;
        self.threads[0].target_cpu = UINT32_MAX;
        self.thread_total = 1;
        self.current_index = 0;
        __atomic_store_n(&self.tick_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&self.switch_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&self.finished_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&self.reclaimed_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&self.wakeup_ipi_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&self.started, false, __ATOMIC_RELAXED);
    }
}

bool create_thread_on_cpu(uint32_t cpu_index, ThreadEntry entry, void* argument) {
    if (entry == nullptr || cpu_index >= configured_cpus ||
        smp::current_cpu_index() != 0) {
        return false;
    }
    const bool scheduling =
        __atomic_load_n(&control_start, __ATOMIC_ACQUIRE);
    // Pre-start create stays IF=0 on the BSP. After start, BSP may recreate
    // into reclaimed Empty slots (typically reusing the mapped stack).
    if (!scheduling && cpu::interrupts_enabled()) {
        return false;
    }

    const uint64_t flags = queue_lock.lock_irqsave();
    PerCpuScheduler& self = processors[cpu_index];
    reclaim_finished_slots(self);
    if (self.thread_total == 0 || self.thread_total >= maximum_threads) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }

    size_t slot = maximum_threads;
    for (size_t index = 1; index < maximum_threads; ++index) {
        if (self.threads[index].state == ThreadState::Empty) {
            slot = index;
            break;
        }
    }
    if (slot == maximum_threads) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }

    Thread& thread = self.threads[slot];
    const size_t worker = slot - 1;
    const uint64_t stack_base = worker_stack_base(cpu_index, worker);
    const bool reuse_stack =
        thread.stack_physical != 0 && thread.stack_base == stack_base;
    if (!reuse_stack) {
        // Allocate/map outside the queue lock (PMM/VMM). M19 allows this while
        // scheduling so Empty slots can be refilled after release_empty_stacks.
        queue_lock.unlock_irqrestore(flags);
        const uint64_t physical = pmm::allocate_pages(thread_stack_pages);
        if (physical == 0) {
            return false;
        }
        if (!vmm::map_kernel_stack(physical, stack_base, thread_stack_pages)) {
            pmm::free_pages(physical, thread_stack_pages);
            return false;
        }
        const uint64_t flags_retry = queue_lock.lock_irqsave();
        PerCpuScheduler& again = processors[cpu_index];
        reclaim_finished_slots(again);
        if (again.threads[slot].state != ThreadState::Empty ||
            again.thread_total >= maximum_threads) {
            queue_lock.unlock_irqrestore(flags_retry);
            (void)vmm::unmap_kernel_stack(stack_base, thread_stack_pages);
            pmm::free_pages(physical, thread_stack_pages);
            return false;
        }
        Thread& assigned = again.threads[slot];
        assigned.stack_base = stack_base;
        assigned.stack_physical = physical;
        prepare_thread_frame(assigned, entry, argument);
        ++again.thread_total;
        queue_lock.unlock_irqrestore(flags_retry);
        return true;
    }

    prepare_thread_frame(thread, entry, argument);
    ++self.thread_total;
    queue_lock.unlock_irqrestore(flags);
    return true;
}

bool create_thread(ThreadEntry entry, void* argument) {
    return create_thread_on_cpu(0, entry, argument);
}

bool release_empty_stacks() {
    if (smp::current_cpu_index() != 0 || configured_cpus == 0) {
        return false;
    }

    struct PendingRelease {
        uint64_t stack_base;
        uint64_t stack_physical;
    };
    PendingRelease pending[smp::maximum_processors * workers_per_cpu];
    size_t pending_count = 0;

    const uint64_t flags = queue_lock.lock_irqsave();
    for (size_t cpu_index = 0; cpu_index < configured_cpus; ++cpu_index) {
        reclaim_finished_slots(processors[cpu_index]);
        for (size_t slot = 1; slot < maximum_threads; ++slot) {
            Thread& thread = processors[cpu_index].threads[slot];
            if (thread.state != ThreadState::Empty ||
                thread.stack_physical == 0) {
                continue;
            }
            if (pending_count >=
                smp::maximum_processors * workers_per_cpu) {
                queue_lock.unlock_irqrestore(flags);
                return false;
            }
            pending[pending_count++] = PendingRelease{thread.stack_base,
                                                      thread.stack_physical};
            thread.stack_base = 0;
            thread.stack_physical = 0;
        }
    }
    queue_lock.unlock_irqrestore(flags);

    for (size_t index = 0; index < pending_count; ++index) {
        if (!vmm::unmap_kernel_stack(pending[index].stack_base,
                                     thread_stack_pages)) {
            return false;
        }
        pmm::free_pages(pending[index].stack_physical, thread_stack_pages);
        __atomic_add_fetch(&released_stack_page_count, thread_stack_pages,
                           __ATOMIC_RELAXED);
    }
    return true;
}

uint64_t released_stack_pages() {
    return __atomic_load_n(&released_stack_page_count, __ATOMIC_ACQUIRE);
}

bool start() {
    if (smp::current_cpu_index() != 0 || cpu::interrupts_enabled() ||
        !apic::initialized() || configured_cpus == 0 ||
        __atomic_load_n(&control_start, __ATOMIC_ACQUIRE)) {
        return false;
    }
    for (size_t index = 0; index < configured_cpus; ++index) {
        if (processors[index].thread_total < 2) {
            return false;
        }
        __atomic_store_n(&processors[index].started, true, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&application_running_count, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&control_start, true, __ATOMIC_RELEASE);
    if (!smp::signal_application_processors() ||
        !wait_for_application_count(
            static_cast<uint32_t>(configured_cpus - 1))) {
        (void)stop();
        return false;
    }

    apic::start_periodic();
    cpu::enable_interrupts();
    return true;
}

bool stop() {
    if (configured_cpus == 0 || smp::current_cpu_index() != 0 ||
        __atomic_load_n(&migration_active, __ATOMIC_ACQUIRE)) {
        return false;
    }
    cpu::disable_interrupts();
    apic::stop_periodic();
    __atomic_store_n(&processors[0].started, false, __ATOMIC_RELEASE);
    __atomic_store_n(&control_start, false, __ATOMIC_RELEASE);
    const bool signalled = smp::signal_application_processors();
    const bool drained = wait_for_application_count(0);
    if (!drained) {
        return false;
    }
    // Once no AP can be inside application_processor_main, the BSP can also
    // clear rows whose AP never left the park loop after a failed start.
    for (size_t index = 0; index < configured_cpus; ++index) {
        __atomic_store_n(&processors[index].started, false, __ATOMIC_RELEASE);
    }
    return signalled && !cpu::interrupts_enabled();
}

bool application_processors_should_start() {
    return __atomic_load_n(&control_start, __ATOMIC_ACQUIRE);
}

void application_processor_main() {
    const uint32_t index = smp::current_cpu_index();
    if (index == 0 || index >= configured_cpus ||
        !application_processors_should_start()) {
        return;
    }
    PerCpuScheduler& self = processors[index];
    if (!__atomic_load_n(&self.started, __ATOMIC_ACQUIRE)) {
        return;
    }

    __atomic_add_fetch(&application_running_count, 1, __ATOMIC_ACQ_REL);
    apic::start_periodic();
    cpu::enable_interrupts();
    while (application_processors_should_start()) {
        cpu::wait_for_interrupt();
    }

    cpu::disable_interrupts();
    apic::stop_periodic();
    __atomic_store_n(&self.started, false, __ATOMIC_RELEASE);
    __atomic_sub_fetch(&application_running_count, 1, __ATOMIC_ACQ_REL);
}

bool sleep(uint64_t duration) {
    PerCpuScheduler* self = current_processor();
    if (duration == 0 || self == nullptr ||
        !__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        self->current_index == 0 || !cpu::interrupts_enabled()) {
        return false;
    }

    Thread* const sleeping = &self->threads[self->current_index];
    const uint64_t flags = queue_lock.lock_irqsave();
    const uint64_t now = __atomic_load_n(&self->tick_count, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        sleeping->state != ThreadState::Running ||
        duration > ~uint64_t{0} - now) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }
    sleeping->wake_tick = now + duration;
    sleeping->state = ThreadState::Sleeping;
    queue_lock.unlock_irqrestore(flags);

    // Slots are never compacted, so this pointer remains stable even when a
    // different local worker migrates. Only the local timer wakes this state.
    while (__atomic_load_n(&sleeping->state, __ATOMIC_ACQUIRE) ==
           ThreadState::Sleeping) {
        cpu::wait_for_interrupt();
    }
    return true;
}

void event_init(Event& event) {
    event.signaled = 0;
    event.waiter_count = 0;
    for (size_t index = 0; index < event_waiter_capacity; ++index) {
        event.waiter_cpu[index] = UINT32_MAX;
        event.waiter_slot[index] = UINT32_MAX;
    }
}

// queue_lock held. Pops a valid FIFO head, marks Ready, returns remote CPU to
// IPI (UINT32_MAX if local). Returns false if the queue is empty or corrupt.
bool wake_waiter_head(Event& event, uint32_t& remote_cpu) {
    remote_cpu = UINT32_MAX;
    while (event.waiter_count > 0) {
        const uint32_t cpu_index = event.waiter_cpu[0];
        const uint32_t slot = event.waiter_slot[0];
        for (uint32_t index = 1; index < event.waiter_count; ++index) {
            event.waiter_cpu[index - 1] = event.waiter_cpu[index];
            event.waiter_slot[index - 1] = event.waiter_slot[index];
        }
        --event.waiter_count;
        event.waiter_cpu[event.waiter_count] = UINT32_MAX;
        event.waiter_slot[event.waiter_count] = UINT32_MAX;
        if (cpu_index >= configured_cpus || slot == 0 ||
            slot >= maximum_threads ||
            processors[cpu_index].threads[slot].state !=
                ThreadState::Waiting) {
            continue;
        }
        processors[cpu_index].threads[slot].state = ThreadState::Ready;
        if (cpu_index != smp::current_cpu_index()) {
            remote_cpu = cpu_index;
        }
        return true;
    }
    return false;
}

bool deliver_wake_ipis(const uint32_t* cpus, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (cpus[index] == UINT32_MAX) {
            continue;
        }
        if (!smp::signal_processor(cpus[index], cpu::scheduler_wakeup_vector)) {
            return false;
        }
    }
    return true;
}

bool wait(Event& event) {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr ||
        !__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        self->current_index == 0 || !cpu::interrupts_enabled()) {
        return false;
    }

    Thread* const waiting = &self->threads[self->current_index];
    const uint32_t cpu_index = smp::current_cpu_index();
    const uint32_t slot = static_cast<uint32_t>(self->current_index);
    const uint64_t flags = queue_lock.lock_irqsave();
    if (!__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        waiting->state != ThreadState::Running) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }
    if (event.signaled > 0) {
        --event.signaled;
        __atomic_add_fetch(&event_waits, 1, __ATOMIC_RELAXED);
        queue_lock.unlock_irqrestore(flags);
        return true;
    }
    if (event.waiter_count >= event_waiter_capacity) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }
    event.waiter_cpu[event.waiter_count] = cpu_index;
    event.waiter_slot[event.waiter_count] = slot;
    ++event.waiter_count;
    waiting->state = ThreadState::Waiting;
    __atomic_add_fetch(&event_waits, 1, __ATOMIC_RELAXED);
    queue_lock.unlock_irqrestore(flags);

    while (__atomic_load_n(&waiting->state, __ATOMIC_ACQUIRE) ==
           ThreadState::Waiting) {
        cpu::wait_for_interrupt();
    }
    return true;
}

bool signal(Event& event) {
    if (configured_cpus == 0) {
        return false;
    }

    uint32_t wake_cpu = UINT32_MAX;
    const uint64_t flags = queue_lock.lock_irqsave();
    __atomic_add_fetch(&event_signals, 1, __ATOMIC_RELAXED);
    if (event.waiter_count == 0) {
        if (event.signaled < UINT32_MAX) {
            ++event.signaled;
        }
        queue_lock.unlock_irqrestore(flags);
        return true;
    }
    if (!wake_waiter_head(event, wake_cpu)) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }
    __atomic_add_fetch(&event_wakes, 1, __ATOMIC_RELAXED);
    queue_lock.unlock_irqrestore(flags);
    return deliver_wake_ipis(&wake_cpu, 1);
}

bool broadcast(Event& event) {
    if (configured_cpus == 0) {
        return false;
    }

    uint32_t wake_cpus[event_waiter_capacity];
    size_t wake_count = 0;
    const uint64_t flags = queue_lock.lock_irqsave();
    __atomic_add_fetch(&event_broadcasts, 1, __ATOMIC_RELAXED);
    if (event.waiter_count == 0) {
        if (event.signaled < UINT32_MAX) {
            ++event.signaled;
        }
        queue_lock.unlock_irqrestore(flags);
        return true;
    }
    while (event.waiter_count > 0 && wake_count < event_waiter_capacity) {
        uint32_t remote = UINT32_MAX;
        if (!wake_waiter_head(event, remote)) {
            break;
        }
        wake_cpus[wake_count++] = remote;
        __atomic_add_fetch(&event_wakes, 1, __ATOMIC_RELAXED);
    }
    queue_lock.unlock_irqrestore(flags);
    return wake_count > 0 && deliver_wake_ipis(wake_cpus, wake_count);
}

void mutex_init(Mutex& mutex) {
    mutex.locked = 0;
    mutex.owner_cpu = UINT32_MAX;
    mutex.owner_slot = UINT32_MAX;
    event_init(mutex.gate);
}

bool mutex_lock(Mutex& mutex) {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr ||
        !__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        self->current_index == 0 || !cpu::interrupts_enabled()) {
        return false;
    }

    Thread* const thread = &self->threads[self->current_index];
    const uint32_t cpu_index = smp::current_cpu_index();
    const uint32_t slot = static_cast<uint32_t>(self->current_index);

    for (;;) {
        const uint64_t flags = queue_lock.lock_irqsave();
        if (!__atomic_load_n(&self->started, __ATOMIC_ACQUIRE)) {
            queue_lock.unlock_irqrestore(flags);
            return false;
        }
        // Cross-CPU unlock may set Ready and IPI before the local timer has
        // switched back; if this slot is still current, claim Running.
        if (thread->state == ThreadState::Ready &&
            self->current_index == slot) {
            thread->state = ThreadState::Running;
        }
        if (thread->state != ThreadState::Running) {
            queue_lock.unlock_irqrestore(flags);
            while (__atomic_load_n(&thread->state, __ATOMIC_ACQUIRE) !=
                   ThreadState::Running) {
                cpu::wait_for_interrupt();
            }
            continue;
        }
        if (mutex.locked == 0) {
            mutex.locked = 1;
            mutex.owner_cpu = cpu_index;
            mutex.owner_slot = slot;
            __atomic_add_fetch(&mutex_locks, 1, __ATOMIC_RELAXED);
            queue_lock.unlock_irqrestore(flags);
            return true;
        }
        if (mutex.owner_cpu == cpu_index && mutex.owner_slot == slot) {
            queue_lock.unlock_irqrestore(flags);
            return false;  // not recursive
        }
        if (mutex.gate.waiter_count >= event_waiter_capacity) {
            queue_lock.unlock_irqrestore(flags);
            return false;
        }
        mutex.gate.waiter_cpu[mutex.gate.waiter_count] = cpu_index;
        mutex.gate.waiter_slot[mutex.gate.waiter_count] = slot;
        ++mutex.gate.waiter_count;
        thread->state = ThreadState::Waiting;
        __atomic_add_fetch(&mutex_waits, 1, __ATOMIC_RELAXED);
        queue_lock.unlock_irqrestore(flags);

        while (__atomic_load_n(&thread->state, __ATOMIC_ACQUIRE) ==
               ThreadState::Waiting) {
            cpu::wait_for_interrupt();
        }
    }
}

bool mutex_unlock(Mutex& mutex) {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr || self->current_index == 0) {
        return false;
    }

    const uint32_t cpu_index = smp::current_cpu_index();
    const uint32_t slot = static_cast<uint32_t>(self->current_index);
    uint32_t wake_cpu = UINT32_MAX;
    const uint64_t flags = queue_lock.lock_irqsave();
    if (mutex.locked == 0 || mutex.owner_cpu != cpu_index ||
        mutex.owner_slot != slot) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }
    mutex.locked = 0;
    mutex.owner_cpu = UINT32_MAX;
    mutex.owner_slot = UINT32_MAX;
    __atomic_add_fetch(&mutex_unlocks, 1, __ATOMIC_RELAXED);
    // No sticky: woken waiter re-acquires under the lock. Do not send vector
    // 50 here — that counter is part of the M10 migration contract; the
    // periodic local timer will observe Ready and switch.
    if (mutex.gate.waiter_count > 0) {
        if (!wake_waiter_head(mutex.gate, wake_cpu)) {
            queue_lock.unlock_irqrestore(flags);
            return false;
        }
    }
    queue_lock.unlock_irqrestore(flags);
    (void)wake_cpu;
    return true;
}

bool migrate_to(uint32_t target_cpu) {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr || target_cpu >= configured_cpus ||
        target_cpu == smp::current_cpu_index() || !cpu::interrupts_enabled() ||
        self->current_index == 0 ||
        !__atomic_load_n(&processors[target_cpu].started, __ATOMIC_ACQUIRE)) {
        return false;
    }

    const uint64_t flags = queue_lock.lock_irqsave();
    Thread& current = self->threads[self->current_index];
    bool available = false;
    for (size_t slot = 1; slot < maximum_threads; ++slot) {
        if (processors[target_cpu].threads[slot].state == ThreadState::Empty) {
            available = true;
            break;
        }
    }
    if (!available || current.state != ThreadState::Running ||
        __atomic_load_n(&migration_active, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&self->started, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&processors[target_cpu].started, __ATOMIC_ACQUIRE)) {
        queue_lock.unlock_irqrestore(flags);
        return false;
    }

    __atomic_store_n(&migration_active, true, __ATOMIC_RELEASE);
    current.target_cpu = target_cpu;
    current.state = ThreadState::MigrationPending;
    queue_lock.unlock_irqrestore(flags);

    // The next local timer saves this exact call frame and retires the thread.
    // A later source tick publishes it; the target timer/IPI resumes this loop.
    // Reaching the target CPU therefore proves the same invocation continued.
    while (smp::current_cpu_index() != target_cpu) {
        cpu::wait_for_interrupt();
    }
    return true;
}

cpu::InterruptFrame* on_timer(cpu::InterruptFrame* frame) {
    if (configured_cpus == 0) {
        __atomic_add_fetch(&early_tick_count, 1, __ATOMIC_RELAXED);
        apic::end_of_interrupt();
        return frame;
    }

    PerCpuScheduler* self = current_processor();
    if (self == nullptr) {
        apic::end_of_interrupt();
        return frame;
    }
    __atomic_add_fetch(&self->tick_count, 1, __ATOMIC_RELAXED);
    cpu::InterruptFrame* next_frame = frame;
    uint32_t wake_targets[maximum_threads] = {};
    size_t wake_count = 0;

    const uint64_t flags = queue_lock.lock_irqsave();
    publish_saved_migrations(*self, smp::current_cpu_index(), wake_targets,
                             wake_count);
    if (__atomic_load_n(&self->started, __ATOMIC_ACQUIRE)) {
        const uint64_t now = __atomic_load_n(&self->tick_count, __ATOMIC_RELAXED);
        for (size_t index = 0; index < maximum_threads; ++index) {
            if (self->threads[index].state == ThreadState::Sleeping &&
                self->threads[index].wake_tick <= now) {
                self->threads[index].state = ThreadState::Ready;
            }
        }

        Thread& current = self->threads[self->current_index];
        current.saved_frame = frame;
        if (current.state == ThreadState::Running) {
            current.state = ThreadState::Ready;
        } else if (current.state == ThreadState::MigrationPending) {
            current.state = ThreadState::MigrationSaved;
        }

        size_t next = self->current_index;
        for (size_t step = 1; step <= maximum_threads; ++step) {
            const size_t candidate =
                (self->current_index + step) % maximum_threads;
            if (self->threads[candidate].state == ThreadState::Ready) {
                next = candidate;
                break;
            }
        }

        if (next != self->current_index) {
            self->threads[next].state = ThreadState::Running;
            self->current_index = next;
            __atomic_add_fetch(&self->switch_count, 1, __ATOMIC_RELAXED);
            next_frame = self->threads[next].saved_frame;
        } else if (current.state != ThreadState::Finished &&
                   current.state != ThreadState::MigrationSaved) {
            current.state = ThreadState::Running;
        }

        // After switching away from a Finished worker, free the slot.
        reclaim_finished_slots(*self);
    }
    queue_lock.unlock_irqrestore(flags);
    apic::end_of_interrupt();

    for (size_t index = 0; index < wake_count; ++index) {
        if (!smp::signal_processor(wake_targets[index],
                                   cpu::scheduler_wakeup_vector)) {
            __atomic_store_n(&migration_fault, true, __ATOMIC_RELEASE);
        }
    }
    return next_frame;
}

void on_wakeup_ipi() {
    PerCpuScheduler* self = current_processor();
    if (self != nullptr) {
        __atomic_add_fetch(&self->wakeup_ipi_count, 1, __ATOMIC_RELAXED);
    }
}

uint64_t ticks() {
    if (configured_cpus == 0) {
        return __atomic_load_n(&early_tick_count, __ATOMIC_RELAXED);
    }
    const PerCpuScheduler* self = current_processor();
    return self == nullptr ? 0
                           : __atomic_load_n(&self->tick_count, __ATOMIC_RELAXED);
}

uint64_t context_switches() {
    const PerCpuScheduler* self = current_processor();
    return self == nullptr
               ? 0
               : __atomic_load_n(&self->switch_count, __ATOMIC_RELAXED);
}

uint64_t finished_threads() {
    uint64_t total = 0;
    for (size_t index = 0; index < configured_cpus; ++index) {
        total += __atomic_load_n(&processors[index].finished_count,
                                 __ATOMIC_ACQUIRE);
    }
    return total;
}

uint64_t reclaimed_threads() {
    uint64_t total = 0;
    for (size_t index = 0; index < configured_cpus; ++index) {
        total += __atomic_load_n(&processors[index].reclaimed_count,
                                 __ATOMIC_ACQUIRE);
    }
    return total;
}

uint64_t event_wait_count() {
    return __atomic_load_n(&event_waits, __ATOMIC_ACQUIRE);
}

uint64_t event_signal_count() {
    return __atomic_load_n(&event_signals, __ATOMIC_ACQUIRE);
}

uint64_t event_broadcast_count() {
    return __atomic_load_n(&event_broadcasts, __ATOMIC_ACQUIRE);
}

uint64_t event_wake_count() {
    return __atomic_load_n(&event_wakes, __ATOMIC_ACQUIRE);
}

uint64_t mutex_lock_count() {
    return __atomic_load_n(&mutex_locks, __ATOMIC_ACQUIRE);
}

uint64_t mutex_unlock_count() {
    return __atomic_load_n(&mutex_unlocks, __ATOMIC_ACQUIRE);
}

uint64_t mutex_wait_count() {
    return __atomic_load_n(&mutex_waits, __ATOMIC_ACQUIRE);
}

size_t thread_count() {
    const PerCpuScheduler* self = current_processor();
    return self == nullptr ? 0 : self->thread_total;
}

size_t current_thread() {
    const PerCpuScheduler* self = current_processor();
    return self == nullptr ? 0 : self->current_index;
}

uint64_t current_thread_stack_base() {
    PerCpuScheduler* self = current_processor();
    if (self == nullptr) {
        return 0;
    }
    const uint64_t flags = queue_lock.lock_irqsave();
    const uint64_t result = self->threads[self->current_index].stack_base;
    queue_lock.unlock_irqrestore(flags);
    return result;
}

bool started() {
    const PerCpuScheduler* self = current_processor();
    return self != nullptr && __atomic_load_n(&self->started, __ATOMIC_ACQUIRE);
}

uint64_t ticks_for_cpu(uint32_t cpu_index) {
    return cpu_index < configured_cpus
               ? __atomic_load_n(&processors[cpu_index].tick_count,
                                 __ATOMIC_RELAXED)
               : 0;
}

uint64_t context_switches_for_cpu(uint32_t cpu_index) {
    return cpu_index < configured_cpus
               ? __atomic_load_n(&processors[cpu_index].switch_count,
                                 __ATOMIC_RELAXED)
               : 0;
}

uint64_t finished_threads_for_cpu(uint32_t cpu_index) {
    return cpu_index < configured_cpus
               ? __atomic_load_n(&processors[cpu_index].finished_count,
                                 __ATOMIC_ACQUIRE)
               : 0;
}

uint64_t reclaimed_threads_for_cpu(uint32_t cpu_index) {
    return cpu_index < configured_cpus
               ? __atomic_load_n(&processors[cpu_index].reclaimed_count,
                                 __ATOMIC_ACQUIRE)
               : 0;
}

size_t thread_count_for_cpu(uint32_t cpu_index) {
    return cpu_index < configured_cpus ? processors[cpu_index].thread_total : 0;
}

size_t configured_processor_count() {
    return configured_cpus;
}

uint64_t migration_count() {
    return __atomic_load_n(&completed_migrations, __ATOMIC_ACQUIRE);
}

uint64_t migration_wakeup_ipi_count(uint32_t cpu_index) {
    return cpu_index < configured_cpus
               ? __atomic_load_n(&processors[cpu_index].wakeup_ipi_count,
                                 __ATOMIC_ACQUIRE)
               : 0;
}

bool migration_failed() {
    return __atomic_load_n(&migration_fault, __ATOMIC_ACQUIRE);
}

}  // namespace cxc::scheduler
