#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
std::int32_t absolute_scheduler_worker_count();
std::uint64_t absolute_scheduler_steal_count();
}

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}

struct FanoutContext {
    std::atomic<std::int32_t> started{0};
    std::atomic<bool> release{false};
    std::mutex threadMutex;
    std::unordered_set<std::thread::id> childThreads;
    std::uint64_t steals = 0;
    bool distributed = false;
};

struct ChildContext {
    FanoutContext* fanout = nullptr;
};

void fanoutChild(void* opaque) {
    auto* child = static_cast<ChildContext*>(opaque);
    {
        std::lock_guard lock(child->fanout->threadMutex);
        child->fanout->childThreads.insert(std::this_thread::get_id());
    }
    child->fanout->started.fetch_add(1, std::memory_order_release);
    while (!child->fanout->release.load(std::memory_order_acquire))
        std::this_thread::yield();
}

void fanoutParent(void* opaque) {
    auto* fanout = static_cast<FanoutContext*>(opaque);
    constexpr std::int32_t childCount = 64;
    const std::int32_t workerCount = absolute_scheduler_worker_count();
    const std::uint64_t stealsBefore = absolute_scheduler_steal_count();
    std::vector<void*> children;
    children.reserve(childCount);

    for (std::int32_t index = 0; index < childCount; ++index) {
        children.push_back(absolute_task_spawn_config(
            fanoutChild, new ChildContext{fanout},
            -1, index % 7 - 3, index % 2 == 0 ? "even" : "odd"));
    }

    const auto timeout =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const std::int32_t expectedThieves = workerCount - 1;
    while (fanout->started.load(std::memory_order_acquire) <
        expectedThieves &&
        std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    fanout->distributed =
        fanout->started.load(std::memory_order_acquire) >=
        expectedThieves;
    fanout->release.store(true, std::memory_order_release);

    for (void* task : children)
        delete static_cast<ChildContext*>(absolute_task_await(task));

    fanout->steals =
        absolute_scheduler_steal_count() - stealsBefore;
}
}  // namespace

int main() {
#if defined(_WIN32)
    _putenv_s("ABSOLUTE_SCHEDULER_WORKERS", "4");
#else
    setenv("ABSOLUTE_SCHEDULER_WORKERS", "4", 1);
#endif

    require(absolute_scheduler_worker_count() == 4);
    auto* fanout = new FanoutContext;
    void* parent = absolute_task_spawn_config(
        fanoutParent, fanout, -1, 0, "fanout-parent");
    fanout = static_cast<FanoutContext*>(absolute_task_await(parent));

    require(fanout->distributed);
    require(fanout->steals >= 3);
    {
        std::lock_guard lock(fanout->threadMutex);
        require(fanout->childThreads.size() >= 3);
    }
    delete fanout;

    std::cout << "runtime-scheduler-work-stealing=ok\n";
    return 0;
}
