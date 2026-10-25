#pragma once

#include <cxc/cpu.hpp>

#include <stdint.h>

namespace cxc {

class SpinLock {
public:
    void lock() {
        while (__atomic_exchange_n(&word_, 1, __ATOMIC_ACQUIRE) != 0) {
            while (__atomic_load_n(&word_, __ATOMIC_RELAXED) != 0) {
                cpu::relax();
            }
        }
    }

    bool try_lock() {
        return __atomic_exchange_n(&word_, 1, __ATOMIC_ACQUIRE) == 0;
    }

    void unlock() {
        __atomic_store_n(&word_, 0, __ATOMIC_RELEASE);
    }

    uint64_t lock_irqsave() {
        const uint64_t flags = cpu::read_flags();
        cpu::disable_interrupts();
        lock();
        return flags;
    }

    void unlock_irqrestore(uint64_t flags) {
        unlock();
        cpu::restore_interrupts(flags);
    }

private:
    uint32_t word_ = 0;
};

}  // namespace cxc
