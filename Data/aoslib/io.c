#include <stdint.h>
#define AOSLIB_SYSCALLS
#define AOSLIB_IO
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