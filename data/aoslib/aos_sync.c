#include <stdint.h>
#include "../include/aoslib.h"

void mutex_init(mutex_t* m) {
    if (m) m->locked = 0;
}

void mutex_lock(mutex_t* m) {
    if (!m) return;
    while (__sync_lock_test_and_set(&m->locked, 1)) {
        thread_yield();
    }
}

void mutex_unlock(mutex_t* m) {
    if (!m) return;
    __sync_lock_release(&m->locked);
}

int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result) {
    int res = func(arg);
    if (res != 0) {
        if (out_result) *out_result = res;
        return 1;
    }

    uint64_t start_tick = get_system_ticks();
    uint64_t timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;

    int spin_count = 0;

    while (1) {
        res = func(arg);
        if (res != 0) break;

        if (spin_count < 1000) {
            spin_count++;
            hal_cpu_relax();
            continue;
        }

        if (timeout_ms != 0) {
            uint64_t current_tick = get_system_ticks();
            uint64_t elapsed = current_tick - start_tick;
            
            if (elapsed >= timeout_ticks) {
                if (out_result) *out_result = 0;
                return 0;
            }
        }

        thread_yield();
    }

    if (out_result) *out_result = res;
    return 1;
}