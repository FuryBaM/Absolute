#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

extern "C" std::uint64_t absolute_managed_create(std::uint64_t size);
extern "C" std::uint64_t absolute_managed_adopt_raw(
    void* pointer, void (*deleter)(void*));
extern "C" void* absolute_managed_get(std::uint64_t handle);
extern "C" void* absolute_managed_require(std::uint64_t handle);
extern "C" void absolute_managed_destroy(std::uint64_t handle);
extern "C" void* absolute_capsule_create_typed(
    std::uint64_t handle, void (*deleter)(void*));
extern "C" void absolute_capsule_destroy(void* capsule);

namespace {
    std::atomic<int> storageDeletes{0};
    std::atomic<int> pointeeDestroys{0};

    void CountedStorageDelete(void* pointer) {
        storageDeletes.fetch_add(1, std::memory_order_relaxed);
        std::free(pointer);
    }

    void CountedPointeeDestroy(void*) {
        pointeeDestroys.fetch_add(1, std::memory_order_relaxed);
    }

    void Require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    }
}

int main() {
    void* external = std::malloc(32);
    Require(external != nullptr, "external allocation failed");
    const std::uint64_t adopted =
        absolute_managed_adopt_raw(external, CountedStorageDelete);
    Require(absolute_managed_require(adopted) == external,
        "adopted pointer lookup failed");
    absolute_managed_destroy(adopted);
    Require(storageDeletes.load(std::memory_order_relaxed) == 1,
        "custom storage deleter was not called exactly once");
    Require(absolute_managed_get(adopted) == nullptr,
        "destroyed adopted handle remained valid");

    const std::uint64_t transferred = absolute_managed_create(16);
    void* capsule =
        absolute_capsule_create_typed(transferred, CountedPointeeDestroy);
    Require(absolute_managed_get(transferred) == nullptr,
        "capsule transfer did not invalidate the sender handle");
    absolute_capsule_destroy(capsule);
    Require(pointeeDestroys.load(std::memory_order_relaxed) == 1,
        "capsule pointee destructor was not called exactly once");

    constexpr int threadCount = 8;
    constexpr int iterations = 5000;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int thread = 0; thread < threadCount; ++thread) {
        workers.emplace_back([] {
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const std::uint64_t handle = absolute_managed_create(24);
                Require(absolute_managed_get(handle) != nullptr,
                    "concurrent managed lookup failed");
                absolute_managed_destroy(handle);
                Require(absolute_managed_get(handle) == nullptr,
                    "expired concurrent handle remained valid");
            }
        });
    }
    for (std::thread& worker : workers) worker.join();

    std::cout << "runtime-managed-memory=ok\n";
    return 0;
}
