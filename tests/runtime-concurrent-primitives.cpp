#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
void* absolute_atomic_create(std::int64_t);
std::int64_t absolute_atomic_fetch_add(void*, std::int64_t);
std::int64_t absolute_atomic_load(void*);
void absolute_atomic_destroy(void*);

void* absolute_cancellation_token_create();
void absolute_cancellation_token_cancel(void*);
bool absolute_cancellation_token_is_cancelled(void*);
void absolute_cancellation_token_destroy(void*);

void* absolute_mutex_create();
void absolute_mutex_lock(void*);
void absolute_mutex_unlock(void*);
bool absolute_mutex_try_lock(void*);
void absolute_mutex_destroy(void*);

void* absolute_semaphore_create(std::int32_t, std::int32_t);
void absolute_semaphore_acquire(void*);
bool absolute_semaphore_try_acquire(void*);
bool absolute_semaphore_release(void*, std::int32_t);
std::int32_t absolute_semaphore_available(void*);
void absolute_semaphore_destroy(void*);

void* absolute_rwlock_create();
void absolute_rwlock_lock_read(void*);
bool absolute_rwlock_try_lock_read(void*);
void absolute_rwlock_unlock_read(void*);
void absolute_rwlock_lock_write(void*);
bool absolute_rwlock_try_lock_write(void*);
void absolute_rwlock_unlock_write(void*);
void absolute_rwlock_destroy(void*);

void* absolute_condition_create();
void absolute_condition_wait(void*, void*);
void absolute_condition_notify_one(void*);
void absolute_condition_notify_all(void*);
void absolute_condition_destroy(void*);

void* absolute_once_create();
bool absolute_once_begin(void*);
void absolute_once_complete(void*);
bool absolute_once_is_complete(void*);
void absolute_once_destroy(void*);
}

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}
}

int main() {
    // Atomic and cancellation handles are deliberately destroyed while other
    // threads are resolving them. An operation that already resolved the
    // handle keeps the state alive; a later operation sees a stale opaque ID
    // and returns its neutral result instead of touching freed storage.
    void* atomicHandle = absolute_atomic_create(0);
    require(atomicHandle != nullptr);
    std::atomic<bool> runAtomic{true};
    std::vector<std::thread> atomicThreads;
    for (std::int32_t index = 0; index < 8; ++index) {
        atomicThreads.emplace_back([&] {
            while (runAtomic.load(std::memory_order_acquire)) {
                absolute_atomic_fetch_add(atomicHandle, 1);
                (void)absolute_atomic_load(atomicHandle);
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    absolute_atomic_destroy(atomicHandle);
    runAtomic.store(false, std::memory_order_release);
    for (auto& thread : atomicThreads) thread.join();
    require(absolute_atomic_load(atomicHandle) == 0);

    void* cancellation = absolute_cancellation_token_create();
    require(cancellation != nullptr);
    std::atomic<bool> runCancellation{true};
    std::vector<std::thread> cancellationThreads;
    for (std::int32_t index = 0; index < 4; ++index) {
        cancellationThreads.emplace_back([&, index] {
            while (runCancellation.load(std::memory_order_acquire)) {
                if ((index & 1) == 0)
                    absolute_cancellation_token_cancel(cancellation);
                else
                    (void)absolute_cancellation_token_is_cancelled(cancellation);
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    absolute_cancellation_token_destroy(cancellation);
    runCancellation.store(false, std::memory_order_release);
    for (auto& thread : cancellationThreads) thread.join();
    require(!absolute_cancellation_token_is_cancelled(cancellation));

    void* staleMutex = absolute_mutex_create();
    require(staleMutex != nullptr);
    absolute_mutex_destroy(staleMutex);
    require(!absolute_mutex_try_lock(staleMutex));

    void* staleSemaphore = absolute_semaphore_create(1, 1);
    require(staleSemaphore != nullptr);
    absolute_semaphore_destroy(staleSemaphore);
    require(!absolute_semaphore_try_acquire(staleSemaphore));
    require(!absolute_semaphore_release(staleSemaphore, 1));
    require(absolute_semaphore_available(staleSemaphore) == 0);

    void* semaphore = absolute_semaphore_create(0, 8);
    require(semaphore != nullptr);
    std::atomic<std::int32_t> passed{0};
    std::vector<std::thread> semaphoreThreads;
    for (std::int32_t index = 0; index < 8; ++index) {
        semaphoreThreads.emplace_back([&] {
            absolute_semaphore_acquire(semaphore);
            passed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(passed.load(std::memory_order_relaxed) == 0);
    require(absolute_semaphore_release(semaphore, 8));
    for (auto& thread : semaphoreThreads) thread.join();
    require(passed.load(std::memory_order_relaxed) == 8);
    absolute_semaphore_destroy(semaphore);

    void* staleRwLock = absolute_rwlock_create();
    require(staleRwLock != nullptr);
    absolute_rwlock_destroy(staleRwLock);
    require(!absolute_rwlock_try_lock_read(staleRwLock));
    require(!absolute_rwlock_try_lock_write(staleRwLock));

    void* staleCondition = absolute_condition_create();
    require(staleCondition != nullptr);
    absolute_condition_destroy(staleCondition);
    absolute_condition_notify_one(staleCondition);
    absolute_condition_notify_all(staleCondition);

    void* staleOnce = absolute_once_create();
    require(staleOnce != nullptr);
    absolute_once_destroy(staleOnce);
    require(!absolute_once_begin(staleOnce));
    require(!absolute_once_is_complete(staleOnce));

    void* rwlock = absolute_rwlock_create();
    require(rwlock != nullptr);
    std::int64_t protectedValue = 0;
    std::atomic<bool> readersValid{true};
    std::vector<std::thread> rwThreads;
    for (std::int32_t index = 0; index < 4; ++index) {
        rwThreads.emplace_back([&] {
            for (std::int32_t iteration = 0; iteration < 1000; ++iteration) {
                absolute_rwlock_lock_write(rwlock);
                ++protectedValue;
                absolute_rwlock_unlock_write(rwlock);
            }
        });
    }
    for (std::int32_t index = 0; index < 4; ++index) {
        rwThreads.emplace_back([&] {
            std::int64_t previous = 0;
            for (std::int32_t iteration = 0; iteration < 1000; ++iteration) {
                absolute_rwlock_lock_read(rwlock);
                const std::int64_t snapshot = protectedValue;
                absolute_rwlock_unlock_read(rwlock);
                if (snapshot < previous)
                    readersValid.store(false, std::memory_order_relaxed);
                previous = snapshot;
            }
        });
    }
    for (auto& thread : rwThreads) thread.join();
    require(readersValid.load(std::memory_order_relaxed));
    require(protectedValue == 4000);
    absolute_rwlock_destroy(rwlock);

    void* mutex = absolute_mutex_create();
    void* condition = absolute_condition_create();
    bool ready = false;
    bool observed = false;
    std::thread waiter([&] {
        absolute_mutex_lock(mutex);
        while (!ready) absolute_condition_wait(condition, mutex);
        observed = true;
        absolute_mutex_unlock(mutex);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    absolute_mutex_lock(mutex);
    ready = true;
    absolute_condition_notify_one(condition);
    absolute_mutex_unlock(mutex);
    waiter.join();
    require(observed);
    absolute_condition_destroy(condition);
    absolute_mutex_destroy(mutex);

    void* once = absolute_once_create();
    std::atomic<std::int32_t> onceCalls{0};
    std::vector<std::thread> onceThreads;
    for (std::int32_t index = 0; index < 16; ++index) {
        onceThreads.emplace_back([&] {
            if (absolute_once_begin(once)) {
                onceCalls.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                absolute_once_complete(once);
            }
        });
    }
    for (auto& thread : onceThreads) thread.join();
    require(onceCalls.load(std::memory_order_relaxed) == 1);
    require(absolute_once_is_complete(once));
    absolute_once_destroy(once);

    std::cout << "runtime-concurrent-primitives=ok\n";
    return 0;
}
