#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

extern "C" bool absolute_error_pending();
extern "C" std::uint64_t absolute_error_type();
extern "C" std::uint64_t absolute_error_take();
extern "C" void absolute_error_set(std::uint64_t handle, std::uint64_t type);
extern "C" void absolute_managed_destroy(std::uint64_t handle);

namespace {
    using TaskEntry = void (*)(void*);

    struct Task {
        std::mutex mutex;
        std::condition_variable completed;
        void* context = nullptr;
        std::uint64_t errorHandle = 0;
        std::uint64_t errorType = 0;
        bool done = false;
    };

    struct Work {
        Task* task = nullptr;
        TaskEntry entry = nullptr;
        int core = -1;
        int priority = 0;
        std::string role;
    };

    thread_local int currentCore = -1;
    thread_local int currentPriority = 0;
    thread_local std::string currentRole;

    class ScopedAffinity {
#if defined(_WIN32)
        DWORD_PTR previous = 0;
#elif defined(__linux__)
        cpu_set_t previous{};
        bool restore = false;
#endif

    public:
        explicit ScopedAffinity(int core) {
            if (core < 0) return;
#if defined(_WIN32)
            constexpr int bitCount = static_cast<int>(sizeof(DWORD_PTR) * 8);
            if (core < bitCount)
                previous = SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << core);
#elif defined(__linux__)
            if (core < CPU_SETSIZE &&
                pthread_getaffinity_np(pthread_self(), sizeof(previous), &previous) == 0) {
                cpu_set_t requested;
                CPU_ZERO(&requested);
                CPU_SET(core, &requested);
                restore = pthread_setaffinity_np(
                    pthread_self(), sizeof(requested), &requested) == 0;
            }
#else
            (void)core;
#endif
        }

        ~ScopedAffinity() {
#if defined(_WIN32)
            if (previous) SetThreadAffinityMask(GetCurrentThread(), previous);
#elif defined(__linux__)
            if (restore)
                pthread_setaffinity_np(pthread_self(), sizeof(previous), &previous);
#endif
        }
    };

    class Scheduler {
        std::mutex mutex;
        std::condition_variable available;
        std::array<std::unordered_map<std::string, std::deque<Work>>, 7> lanes;
        std::array<std::deque<std::string>, 7> readyRoles;
        std::vector<std::thread> workers;
        size_t queued = 0;
        bool stopping = false;

        void Run() {
            while (true) {
                Work work;
                {
                    std::unique_lock lock(mutex);
                    available.wait(lock, [&] { return stopping || queued != 0; });
                    if (stopping && queued == 0) return;
                    for (size_t bucket = readyRoles.size(); bucket > 0; --bucket) {
                        const size_t index = bucket - 1;
                        if (readyRoles[index].empty()) continue;
                        std::string role = std::move(readyRoles[index].front());
                        readyRoles[index].pop_front();
                        auto lane = lanes[index].find(role);
                        work = std::move(lane->second.front());
                        lane->second.pop_front();
                        --queued;
                        if (lane->second.empty()) lanes[index].erase(lane);
                        else readyRoles[index].push_back(std::move(role));
                        break;
                    }
                }

                currentCore = work.core;
                currentPriority = work.priority;
                currentRole = work.role;
                ScopedAffinity affinity(work.core);
                try {
                    work.entry(work.task->context);
                    if (absolute_error_pending()) {
                        work.task->errorType = absolute_error_type();
                        work.task->errorHandle = absolute_error_take();
                    }
                }
                catch (...) {
                    std::cerr << "Absolute runtime error: an async task threw across the runtime boundary\n";
                    std::abort();
                }
                {
                    std::lock_guard lock(work.task->mutex);
                    work.task->done = true;
                }
                work.task->completed.notify_all();
                currentCore = -1;
                currentPriority = 0;
                currentRole.clear();
            }
        }

    public:
        Scheduler() {
            const unsigned count = std::min(32U,
                std::max(1U, std::thread::hardware_concurrency()));
            workers.reserve(count);
            for (unsigned index = 0; index < count; ++index)
                workers.emplace_back([this] { Run(); });
        }

        ~Scheduler() {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            available.notify_all();
            for (std::thread& worker : workers)
                if (worker.joinable()) worker.join();
        }

        void Submit(Task* task, TaskEntry entry, int core, int priority, std::string role) {
            {
                std::lock_guard lock(mutex);
                const size_t bucket = static_cast<size_t>(priority + 3);
                std::deque<Work>& lane = lanes[bucket][role];
                if (lane.empty()) readyRoles[bucket].push_back(role);
                lane.push_back({task, entry, core, priority, std::move(role)});
                ++queued;
            }
            available.notify_one();
        }
    };

    Scheduler& GetScheduler() {
        static Scheduler scheduler;
        return scheduler;
    }

    void Wait(Task& task) {
        std::unique_lock lock(task.mutex);
        task.completed.wait(lock, [&] { return task.done; });
    }
}

extern "C" void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core, std::int32_t priority,
    const char* role) {
    if (!entry || !context) {
        std::cerr << "Absolute runtime error: invalid task entry or context\n";
        std::abort();
    }
    if (core < -1 || priority < -3 || priority > 3) {
        std::cerr << "Absolute runtime error: invalid task scheduling options\n";
        std::abort();
    }
    Task* task = new Task;
    task->context = context;
    GetScheduler().Submit(task, entry, core, priority, role ? role : "");
    return task;
}

extern "C" void* absolute_task_spawn(void (*entry)(void*), void* context) {
    return absolute_task_spawn_config(entry, context, -1, 0, nullptr);
}

extern "C" std::int32_t absolute_task_current_core() { return currentCore; }

extern "C" std::int32_t absolute_task_current_priority() { return currentPriority; }

extern "C" const char* absolute_task_current_role() { return currentRole.c_str(); }

extern "C" bool absolute_task_current_role_is(const char* role) {
    return role && currentRole == role;
}

extern "C" void* absolute_task_await(void* handle) {
    if (!handle) {
        std::cerr << "Absolute runtime error: null task is awaited\n";
        std::abort();
    }
    Task* task = static_cast<Task*>(handle);
    Wait(*task);
    void* context = task->context;
    if (task->errorHandle)
        absolute_error_set(task->errorHandle, task->errorType);
    delete task;
    return context;
}

extern "C" void absolute_task_destroy(void* handle) {
    if (!handle) return;
    Task* task = static_cast<Task*>(handle);
    Wait(*task);
    if (task->errorHandle) absolute_managed_destroy(task->errorHandle);
    std::free(task->context);
    delete task;
}

namespace {
    struct CancellationTokenImpl {
        std::atomic<bool> cancelled{false};
    };

    struct ChannelImpl {
        std::mutex mutex;
        std::condition_variable cv_send;
        std::condition_variable cv_recv;
        std::deque<std::int64_t> queue;
        std::int32_t capacity = 0;
        bool closed = false;
    };
}

extern "C" void absolute_task_delay(std::int32_t ms) {
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

extern "C" bool absolute_task_await_timeout(void* handle, std::int32_t ms) {
    if (!handle) return false;
    Task* task = static_cast<Task*>(handle);
    std::unique_lock lock(task->mutex);
    if (ms <= 0) {
        return task->done;
    }
    return task->completed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return task->done; });
}

extern "C" std::int32_t absolute_task_when_any(void** handles, std::int32_t count) {
    if (!handles || count <= 0) return -1;
    while (true) {
        for (std::int32_t i = 0; i < count; ++i) {
            if (handles[i]) {
                Task* task = static_cast<Task*>(handles[i]);
                std::lock_guard lock(task->mutex);
                if (task->done) return i;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

extern "C" void absolute_task_when_all(void** handles, std::int32_t count) {
    if (!handles || count <= 0) return;
    for (std::int32_t i = 0; i < count; ++i) {
        if (handles[i]) {
            Task* task = static_cast<Task*>(handles[i]);
            Wait(*task);
        }
    }
}

extern "C" void* absolute_cancellation_token_create() {
    return new CancellationTokenImpl();
}

extern "C" void absolute_cancellation_token_cancel(void* token) {
    if (!token) return;
    static_cast<CancellationTokenImpl*>(token)->cancelled.store(true);
}

extern "C" bool absolute_cancellation_token_is_cancelled(void* token) {
    if (!token) return false;
    return static_cast<CancellationTokenImpl*>(token)->cancelled.load();
}

extern "C" void absolute_cancellation_token_destroy(void* token) {
    if (!token) return;
    delete static_cast<CancellationTokenImpl*>(token);
}

extern "C" void* absolute_channel_create(std::int32_t capacity) {
    ChannelImpl* ch = new ChannelImpl();
    ch->capacity = capacity > 0 ? capacity : 0;
    return ch;
}

extern "C" bool absolute_channel_send(void* ch, std::int64_t val) {
    if (!ch) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::unique_lock lock(channel->mutex);
    channel->cv_send.wait(lock, [&] {
        return channel->closed || channel->capacity == 0 || static_cast<std::int32_t>(channel->queue.size()) < channel->capacity;
    });
    if (channel->closed) return false;
    channel->queue.push_back(val);
    channel->cv_recv.notify_one();
    return true;
}

extern "C" bool absolute_channel_try_send(void* ch, std::int64_t val) {
    if (!ch) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    if (channel->closed) return false;
    if (channel->capacity > 0 && static_cast<std::int32_t>(channel->queue.size()) >= channel->capacity)
        return false;
    channel->queue.push_back(val);
    channel->cv_recv.notify_one();
    return true;
}

extern "C" std::int64_t absolute_channel_receive(void* ch) {
    if (!ch) return 0;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::unique_lock lock(channel->mutex);
    channel->cv_recv.wait(lock, [&] {
        return channel->closed || !channel->queue.empty();
    });
    if (channel->queue.empty()) return 0;
    std::int64_t val = channel->queue.front();
    channel->queue.pop_front();
    channel->cv_send.notify_one();
    return val;
}

extern "C" bool absolute_channel_try_receive(void* ch, std::int64_t* outVal) {
    if (!ch || !outVal) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    if (channel->queue.empty()) return false;
    *outVal = channel->queue.front();
    channel->queue.pop_front();
    channel->cv_send.notify_one();
    return true;
}

extern "C" void absolute_channel_close(void* ch) {
    if (!ch) return;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    {
        std::lock_guard lock(channel->mutex);
        channel->closed = true;
    }
    channel->cv_recv.notify_all();
    channel->cv_send.notify_all();
}

extern "C" bool absolute_channel_is_closed(void* ch) {
    if (!ch) return true;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    return channel->closed;
}

extern "C" void absolute_channel_destroy(void* ch) {
    if (!ch) return;
    delete static_cast<ChannelImpl*>(ch);
}

extern "C" void* absolute_atomic_create(std::int64_t initialValue) {
    return new std::atomic<std::int64_t>(initialValue);
}

extern "C" std::int64_t absolute_atomic_fetch_add(void* atomic, std::int64_t val) {
    if (!atomic) return 0;
    return static_cast<std::atomic<std::int64_t>*>(atomic)->fetch_add(val);
}

extern "C" std::int64_t absolute_atomic_fetch_sub(void* atomic, std::int64_t val) {
    if (!atomic) return 0;
    return static_cast<std::atomic<std::int64_t>*>(atomic)->fetch_sub(val);
}

extern "C" std::int64_t absolute_atomic_load(void* atomic) {
    if (!atomic) return 0;
    return static_cast<std::atomic<std::int64_t>*>(atomic)->load();
}

extern "C" void absolute_atomic_store(void* atomic, std::int64_t val) {
    if (!atomic) return;
    static_cast<std::atomic<std::int64_t>*>(atomic)->store(val);
}

extern "C" bool absolute_atomic_compare_exchange(void* atomic, std::int64_t expected, std::int64_t desired) {
    if (!atomic) return false;
    std::int64_t exp = expected;
    return static_cast<std::atomic<std::int64_t>*>(atomic)->compare_exchange_strong(exp, desired);
}

extern "C" void absolute_atomic_destroy(void* atomic) {
    if (!atomic) return;
    delete static_cast<std::atomic<std::int64_t>*>(atomic);
}

extern "C" void* absolute_mutex_create() {
    return new std::mutex();
}

extern "C" void absolute_mutex_lock(void* mutex) {
    if (!mutex) return;
    static_cast<std::mutex*>(mutex)->lock();
}

extern "C" void absolute_mutex_unlock(void* mutex) {
    if (!mutex) return;
    static_cast<std::mutex*>(mutex)->unlock();
}

extern "C" bool absolute_mutex_try_lock(void* mutex) {
    if (!mutex) return false;
    return static_cast<std::mutex*>(mutex)->try_lock();
}

extern "C" void absolute_mutex_destroy(void* mutex) {
    if (!mutex) return;
    delete static_cast<std::mutex*>(mutex);
}

