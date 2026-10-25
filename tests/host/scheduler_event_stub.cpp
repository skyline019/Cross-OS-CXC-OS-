// Host-only sticky Event stub so cxc::net::socket can link without the kernel
// scheduler. wait() never blocks; it only consumes a sticky signal.
#include <cxc/scheduler.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::scheduler {

void event_init(Event& event) {
    event.signaled = 0;
    event.waiter_count = 0;
    for (size_t index = 0; index < event_waiter_capacity; ++index) {
        event.waiter_cpu[index] = UINT32_MAX;
        event.waiter_slot[index] = UINT32_MAX;
    }
}

bool wait(Event& event) {
    if (event.signaled > 0) {
        --event.signaled;
        return true;
    }
    return false;
}

bool signal(Event& event) {
    if (event.waiter_count == 0) {
        if (event.signaled < UINT32_MAX) {
            ++event.signaled;
        }
        return true;
    }
    // Host stub has no real waiters; clear one slot and sticky-post.
    --event.waiter_count;
    if (event.signaled < UINT32_MAX) {
        ++event.signaled;
    }
    return true;
}

bool broadcast(Event& event) {
    event.waiter_count = 0;
    if (event.signaled < UINT32_MAX) {
        ++event.signaled;
    }
    return true;
}

bool event_try_consume(Event& event) {
    if (event.signaled > 0) {
        --event.signaled;
        return true;
    }
    return false;
}

}  // namespace cxc::scheduler
