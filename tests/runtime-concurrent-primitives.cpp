#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
void* absolute_mutex_create();
void absolute_mutex_lock(void*);
void absolute_mutex_unlock(void*);
void absolute_mutex_destroy(void*);

void* absolute_semaphore_create(std::int32_t, std::int32_t);
void absolute_semaphore_acquire(void*);
bool absolute_semaphore_release(void*, std::int32_t);
void absolute_semaphore_destroy(void*);

void* absolute_rwlock_create();
void absolute_rwlock_lock_read(void*);
void absolute_rwlock_unlock_read(void*);
void absolute_rwlock_lock_write(void*);
void absolute_rwlock_unlock_write(void*);
void absolute_rwlock_destroy(void*);

void* absolute_condition_create();
void absolute_condition_wait(void*, void*);
void absolute_condition_notify_one(void*);
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
