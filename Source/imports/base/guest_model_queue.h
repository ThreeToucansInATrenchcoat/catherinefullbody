#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum { MODEL_QUEUE_CAPACITY = 10240, MODEL_SEMAPHORE_COUNT = 0x14, MODEL_SEMAPHORE_SIZE = 0x28 };

static void *volatile model_queue_address;
static volatile LONG model_queue_waiters[2];

static void model_queue_register(uintptr_t queue) {
    char setting[2];
    if (GetEnvironmentVariableA("CATHERINE_Q1_NATIVE_SEMAPHORES", setting, sizeof(setting)) != 1 ||
        setting[0] != '1') return;

    // Verify both SDK layouts, including their empty listener lists, before publishing to workers.
    const uintptr_t free_slots = queue + MODEL_SEMAPHORE_SIZE;
    if ((queue & 7) != 0 || LD32(queue + 0x90) != MODEL_QUEUE_CAPACITY ||
        LD64(queue) != queue || LD64(queue + 8) != queue ||
        LD64(free_slots) != free_slots || LD64(free_slots + 8) != free_slots ||
        LD32(queue + MODEL_SEMAPHORE_COUNT) != 0 || LD32(queue + 0x18) != MODEL_QUEUE_CAPACITY ||
        LD32(free_slots + MODEL_SEMAPHORE_COUNT) != MODEL_QUEUE_CAPACITY ||
        LD32(free_slots + 0x18) != MODEL_QUEUE_CAPACITY) {
        fputs("Catherine model queue layout differs from the supported build\n", stderr);
        return;
    }
    if (InterlockedCompareExchangePointer(&model_queue_address, (void *)queue, NULL) == NULL)
        fputs("Catherine native model queue registered before worker start\n", stderr);
}

static volatile LONG *model_queue_waiter_count(uintptr_t sem) {
    const uintptr_t queue = (uintptr_t)__atomic_load_n(&model_queue_address, __ATOMIC_ACQUIRE);
    if (!queue) return NULL;
    if (sem == queue) return &model_queue_waiters[0];
    if (sem == queue + MODEL_SEMAPHORE_SIZE) return &model_queue_waiters[1];
    return NULL;
}

static bool model_queue_try_take(volatile LONG *count) {
    LONG available = __atomic_load_n(count, __ATOMIC_SEQ_CST);
    while (available != 0) {
        const LONG observed = InterlockedCompareExchange(count, (LONG)((uint32_t)available - 1), available);
        if (observed == available) return true;
        available = observed;
    }
    return false;
}

static void model_queue_acquire(Cpu *c) {
    const uintptr_t sem = c->x[0];
    volatile LONG *waiters = model_queue_waiter_count(sem);
    if (!waiters) {
        __attribute__((musttail)) return f_70005c88c0(c);
    }
    volatile LONG *count = (volatile LONG *)(sem + MODEL_SEMAPHORE_COUNT);
    if (!model_queue_try_take(count)) {
        // Announce before rechecking the count to keep a concurrent release from missing us.
        InterlockedIncrement(waiters);
        while (!model_queue_try_take(count)) {
            LONG zero = 0;
            if (!WaitOnAddress(count, &zero, sizeof(zero), INFINITE)) {
                fprintf(stderr, "Catherine model queue WaitOnAddress failed (%lu)\n", GetLastError());
                abort();
            }
        }
        InterlockedDecrement(waiters);
    }
    c->x[0] = sem + 0x1c;  // Preserve the guest UnlockMutex result register.
}

static void model_queue_release(Cpu *c) {
    const uintptr_t sem = c->x[0];
    volatile LONG *waiters = model_queue_waiter_count(sem);
    if (!waiters) {
        __attribute__((musttail)) return f_70005c88d0(c);
    }
    volatile LONG *count = (volatile LONG *)(sem + MODEL_SEMAPHORE_COUNT);
    InterlockedExchangeAdd(count, (LONG)c->x[1]);
    if (__atomic_load_n(waiters, __ATOMIC_SEQ_CST) > 0) WakeByAddressAll((void *)count);
    c->x[1] = UINT32_MAX;  // The guest semaphore release broadcasts with count -1.
    c->x[0] = sem + 0x1c;
}

static void model_queue_count(Cpu *c) {
    const uintptr_t sem = c->x[0];
    if (!model_queue_waiter_count(sem)) {
        __attribute__((musttail)) return f_70005c88f0(c);
    }
    const volatile LONG *count = (const volatile LONG *)(sem + MODEL_SEMAPHORE_COUNT);
    c->x[0] = (uint32_t)__atomic_load_n(count, __ATOMIC_SEQ_CST);
}
