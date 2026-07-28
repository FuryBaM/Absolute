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
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "scheduler_fiber.h"

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
extern "C" void absolute_capsule_destroy(void* capsule);
extern "C" bool absolute_channel_receive_checked(void* channel, std::int64_t* value);

namespace {
    using TaskEntry = void (*)(void*);
    using FiberContext = Absolute::RuntimeDetail::FiberContext;

    enum class TaskState {
        Pending,
        Runnable,
        Running,
        Suspending,
        Suspended,
        Done
    };

    struct Task {
        std::mutex mutex;
        std::condition_variable completed;
        void* context = nullptr;
        TaskEntry entry = nullptr;
        int core = -1;
        int priority = 0;
        int ownerWorker = -1;
        std::string role;
        FiberContext fiber;
        std::vector<Task*> completionWaiters;
        std::uint64_t errorHandle = 0;
        std::uint64_t errorType = 0;
        TaskState state = TaskState::Pending;
        bool wakePending = false;
        bool finished = false;
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
    class Scheduler;
    thread_local Scheduler* currentScheduler = nullptr;
    thread_local Task* currentTask = nullptr;
    thread_local FiberContext* currentWorkerFiber = nullptr;
    thread_local int currentWorkerIndex = -1;
    std::atomic<Scheduler*> schedulerInstance{nullptr};

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
        std::vector<std::deque<Work>> pinned;
        std::vector<std::thread> workers;
        struct Timer {
            std::chrono::steady_clock::time_point deadline;
            std::uint64_t sequence = 0;
            Task* task = nullptr;
        };
        struct TimerLater {
            bool operator()(const Timer& left, const Timer& right) const {
                if (left.deadline != right.deadline)
                    return left.deadline > right.deadline;
                return left.sequence > right.sequence;
            }
        };
        std::priority_queue<Timer, std::vector<Timer>, TimerLater> timers;
        size_t queued = 0;
        std::uint64_t nextTimerSequence = 0;
        bool stopping = false;

        bool TakeLocked(Work& work, int workerIndex) {
            if (queued == 0) return false;
            std::deque<Work>& local = pinned[static_cast<size_t>(workerIndex)];
            if (!local.empty()) {
                work = std::move(local.front());
                local.pop_front();
                --queued;
                return true;
            }
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
                return true;
            }
            return false;
        }

        void EnqueueLocked(Task* task) {
            if (task->ownerWorker >= 0) {
                pinned[static_cast<size_t>(task->ownerWorker)].push_back({
                    task, task->entry, task->core, task->priority, task->role});
                ++queued;
                return;
            }
            const size_t bucket = static_cast<size_t>(task->priority + 3);
            std::deque<Work>& lane = lanes[bucket][task->role];
            if (lane.empty()) readyRoles[bucket].push_back(task->role);
            lane.push_back({
                task, task->entry, task->core, task->priority, task->role});
            ++queued;
        }

        void WakeLocked(Task* task) {
            std::lock_guard taskLock(task->mutex);
            if (task->state == TaskState::Suspending) {
                task->wakePending = true;
            }
            else if (task->state == TaskState::Suspended) {
                task->state = TaskState::Runnable;
                EnqueueLocked(task);
            }
        }

        void PromoteTimersLocked() {
            const auto now = std::chrono::steady_clock::now();
            while (!timers.empty() && timers.top().deadline <= now) {
                Task* task = timers.top().task;
                timers.pop();
                WakeLocked(task);
            }
        }

        static void TaskMain(void* opaque) {
            Task* task = static_cast<Task*>(opaque);
            try {
                task->entry(task->context);
                if (absolute_error_pending()) {
                    task->errorType = absolute_error_type();
                    task->errorHandle = absolute_error_take();
                }
            }
            catch (...) {
                std::cerr << "Absolute runtime error: an async task threw across the runtime boundary\n";
                std::abort();
            }
            task->finished = true;
            task->fiber.SwitchTo(*currentWorkerFiber);
            std::abort();
        }

        void Execute(Work work) {
            const int previousCore = currentCore;
            const int previousPriority = currentPriority;
            std::string previousRole = std::move(currentRole);
            currentCore = work.core;
            currentPriority = work.priority;
            currentRole = work.role;
            currentTask = work.task;
            ScopedAffinity affinity(work.core);

            if (work.task->ownerWorker < 0)
                work.task->ownerWorker = currentWorkerIndex;
            if (!work.task->fiber.IsCreated())
                work.task->fiber.Create(&Scheduler::TaskMain, work.task);
            {
                std::lock_guard taskLock(work.task->mutex);
                work.task->state = TaskState::Running;
            }
            currentWorkerFiber->SwitchTo(work.task->fiber);

            if (work.task->finished) {
                work.task->fiber.Destroy();
                std::vector<Task*> waiters;
                {
                    // Completion and notification stay under the same lock:
                    // await may delete Task immediately after observing done.
                    std::lock_guard taskLock(work.task->mutex);
                    work.task->state = TaskState::Done;
                    work.task->done = true;
                    waiters.swap(work.task->completionWaiters);
                    work.task->completed.notify_all();
                }
                for (Task* waiter : waiters)
                    Resume(waiter);
            }
            else {
                bool enqueue = false;
                {
                    std::lock_guard taskLock(work.task->mutex);
                    if (work.task->state != TaskState::Suspending) std::abort();
                    if (work.task->wakePending) {
                        work.task->wakePending = false;
                        work.task->state = TaskState::Runnable;
                        enqueue = true;
                    }
                    else {
                        work.task->state = TaskState::Suspended;
                    }
                }
                if (enqueue) Enqueue(work.task);
            }
            currentTask = nullptr;
            currentCore = previousCore;
            currentPriority = previousPriority;
            currentRole = std::move(previousRole);
        }

        void Run(int workerIndex) {
            FiberContext workerFiber;
            workerFiber.InitializeThreadRoot();
            currentScheduler = this;
            currentWorkerFiber = &workerFiber;
            currentWorkerIndex = workerIndex;
            while (true) {
                Work work;
                {
                    std::unique_lock lock(mutex);
                    while (true) {
                        PromoteTimersLocked();
                        if (TakeLocked(work, workerIndex)) break;
                        if (stopping && queued == 0) break;
                        if (timers.empty()) {
                            available.wait(lock);
                        }
                        else {
                            available.wait_until(lock, timers.top().deadline);
                        }
                    }
                    if (stopping && queued == 0) break;
                    if (!work.task) continue;
                }
                Execute(std::move(work));
            }
            currentWorkerIndex = -1;
            currentWorkerFiber = nullptr;
            currentScheduler = nullptr;
            workerFiber.DestroyThreadRoot();
        }

        static unsigned ResolveWorkerCount() {
            constexpr unsigned maximumWorkers = 32;
            if (const char* configured = std::getenv("ABSOLUTE_SCHEDULER_WORKERS")) {
                char* end = nullptr;
                const unsigned long value = std::strtoul(configured, &end, 10);
                if (end != configured && *end == '\0' && value >= 1 &&
                    value <= maximumWorkers) {
                    return static_cast<unsigned>(value);
                }
                std::cerr << "Absolute runtime warning: ignoring invalid "
                    "ABSOLUTE_SCHEDULER_WORKERS='" << configured << "'\n";
            }
            const unsigned hardware = std::thread::hardware_concurrency();
            return std::min(maximumWorkers, std::max(1U, hardware));
        }

    public:
        Scheduler() {
            schedulerInstance.store(this, std::memory_order_release);
            const unsigned count = ResolveWorkerCount();
            pinned.resize(count);
            workers.reserve(count);
            for (unsigned index = 0; index < count; ++index)
                workers.emplace_back([this, index] {
                    Run(static_cast<int>(index));
                });
        }

        ~Scheduler() {
            schedulerInstance.store(nullptr, std::memory_order_release);
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            available.notify_all();
            for (std::thread& worker : workers)
                if (worker.joinable()) worker.join();
        }

        void Submit(Task* task, TaskEntry entry, int core, int priority, std::string role) {
            task->entry = entry;
            task->core = core;
            task->priority = priority;
            task->role = role;
            task->state = TaskState::Runnable;
            {
                std::lock_guard lock(mutex);
                const size_t bucket = static_cast<size_t>(priority + 3);
                std::deque<Work>& lane = lanes[bucket][role];
                if (lane.empty()) readyRoles[bucket].push_back(role);
                lane.push_back({task, entry, core, priority, std::move(role)});
                ++queued;
            }
            NotifyProgress();
        }

        void Enqueue(Task* task) {
            {
                std::lock_guard lock(mutex);
                EnqueueLocked(task);
            }
            NotifyProgress();
        }

        void NotifyProgress() {
            available.notify_all();
        }

        void PrepareSuspend() {
            if (!currentTask) std::abort();
            std::lock_guard taskLock(currentTask->mutex);
            if (currentTask->state != TaskState::Running) std::abort();
            currentTask->state = TaskState::Suspending;
        }

        void YieldCurrent() {
            if (!currentTask || !currentWorkerFiber) std::abort();
            currentTask->fiber.SwitchTo(*currentWorkerFiber);
        }

        void Resume(Task* task) {
            if (!task) return;
            {
                std::lock_guard lock(mutex);
                WakeLocked(task);
            }
            NotifyProgress();
        }

        void SuspendUntil(std::chrono::steady_clock::time_point deadline) {
            PrepareSuspend();
            {
                std::lock_guard lock(mutex);
                timers.push({deadline, nextTimerSequence++, currentTask});
            }
            NotifyProgress();
            YieldCurrent();
        }

        std::int32_t WorkerCount() const {
            return static_cast<std::int32_t>(workers.size());
        }
    };

    Scheduler& GetScheduler() {
        static Scheduler scheduler;
        return scheduler;
    }

    void NotifySchedulerProgress() {
        if (Scheduler* scheduler = schedulerInstance.load(std::memory_order_acquire))
            scheduler->NotifyProgress();
    }

    void ResumeSchedulerTask(Task* task) {
        if (Scheduler* scheduler = schedulerInstance.load(std::memory_order_acquire))
            scheduler->Resume(task);
    }

    void Wait(Task& task) {
        if (currentTask) {
            if (currentTask == &task) {
                std::cerr << "Absolute runtime error: task cannot await itself\n";
                std::abort();
            }
            while (true) {
                std::unique_lock lock(task.mutex);
                if (task.done) return;
                task.completionWaiters.push_back(currentTask);
                currentScheduler->PrepareSuspend();
                lock.unlock();
                currentScheduler->YieldCurrent();
            }
        }
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

extern "C" std::int32_t absolute_scheduler_worker_count() {
    return GetScheduler().WorkerCount();
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

    struct MutexImpl {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task*> waiters;
        bool locked = false;
    };

    struct ChannelImpl {
        std::mutex mutex;
        std::condition_variable cv_send;
        std::condition_variable cv_recv;
        std::deque<std::int64_t> queue;
        std::deque<Task*> sendWaiters;
        std::deque<Task*> receiveWaiters;
        std::int32_t capacity = 0;
        bool closed = false;
    };

    struct TransferChannelImpl {
        std::mutex mutex;
        std::condition_variable cv_send;
        std::condition_variable cv_recv;
        std::deque<void*> queue;
        std::deque<Task*> sendWaiters;
        std::deque<Task*> receiveWaiters;
        std::int32_t capacity = 0;
        bool closed = false;
    };
}

extern "C" void absolute_task_delay(std::int32_t ms) {
    if (ms <= 0) return;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(ms);
    if (currentTask) {
        currentScheduler->SuspendUntil(deadline);
        return;
    }
    std::this_thread::sleep_until(deadline);
}

extern "C" bool absolute_task_await_timeout(void* handle, std::int32_t ms) {
    if (!handle) return false;
    Task* task = static_cast<Task*>(handle);
    const auto done = [&] {
        std::lock_guard lock(task->mutex);
        return task->done;
    };
    if (ms <= 0) {
        return done();
    }
    if (currentTask) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(ms);
        while (!done()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return false;
            currentScheduler->SuspendUntil(
                std::min(deadline, now + std::chrono::milliseconds(1)));
        }
        return true;
    }
    std::unique_lock lock(task->mutex);
    return task->completed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return task->done; });
}

extern "C" std::int32_t absolute_task_when_any(void** handles, std::int32_t count) {
    if (!handles || count <= 0) return -1;
    const auto completedIndex = [&] {
        for (std::int32_t i = 0; i < count; ++i) {
            if (handles[i]) {
                Task* task = static_cast<Task*>(handles[i]);
                std::lock_guard lock(task->mutex);
                if (task->done) return i;
            }
        }
        return std::int32_t{-1};
    };
    if (currentTask) {
        while (true) {
            const std::int32_t index = completedIndex();
            if (index >= 0) return index;
            currentScheduler->SuspendUntil(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
        }
    }
    while (true) {
        const std::int32_t index = completedIndex();
        if (index >= 0) return index;
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
    NotifySchedulerProgress();
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
    while (true) {
        Task* receiver = nullptr;
        std::unique_lock lock(channel->mutex);
        if (channel->closed) return false;
        if (channel->capacity == 0 ||
            static_cast<std::int32_t>(channel->queue.size()) < channel->capacity) {
            channel->queue.push_back(val);
            if (!channel->receiveWaiters.empty()) {
                receiver = channel->receiveWaiters.front();
                channel->receiveWaiters.pop_front();
            }
            lock.unlock();
            channel->cv_recv.notify_one();
            ResumeSchedulerTask(receiver);
            return true;
        }
        if (!currentTask) {
            channel->cv_send.wait(lock, [&] {
                return channel->closed ||
                    static_cast<std::int32_t>(channel->queue.size()) < channel->capacity;
            });
            continue;
        }
        channel->sendWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_channel_try_send(void* ch, std::int64_t val) {
    if (!ch) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    Task* receiver = nullptr;
    std::unique_lock lock(channel->mutex);
    if (channel->closed) return false;
    if (channel->capacity > 0 && static_cast<std::int32_t>(channel->queue.size()) >= channel->capacity)
        return false;
    channel->queue.push_back(val);
    if (!channel->receiveWaiters.empty()) {
        receiver = channel->receiveWaiters.front();
        channel->receiveWaiters.pop_front();
    }
    lock.unlock();
    channel->cv_recv.notify_one();
    ResumeSchedulerTask(receiver);
    return true;
}

extern "C" std::int64_t absolute_channel_receive(void* ch) {
    std::int64_t value = 0;
    absolute_channel_receive_checked(ch, &value);
    return value;
}

extern "C" bool absolute_channel_receive_checked(void* ch, std::int64_t* outVal) {
    if (!ch || !outVal) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    while (true) {
        Task* sender = nullptr;
        std::unique_lock lock(channel->mutex);
        if (!channel->queue.empty()) {
            *outVal = channel->queue.front();
            channel->queue.pop_front();
            if (!channel->sendWaiters.empty()) {
                sender = channel->sendWaiters.front();
                channel->sendWaiters.pop_front();
            }
            lock.unlock();
            channel->cv_send.notify_one();
            ResumeSchedulerTask(sender);
            return true;
        }
        if (channel->closed) return false;
        if (!currentTask) {
            channel->cv_recv.wait(lock, [&] {
                return channel->closed || !channel->queue.empty();
            });
            continue;
        }
        channel->receiveWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_channel_try_receive(void* ch, std::int64_t* outVal) {
    if (!ch || !outVal) return false;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    Task* sender = nullptr;
    std::unique_lock lock(channel->mutex);
    if (channel->queue.empty()) return false;
    *outVal = channel->queue.front();
    channel->queue.pop_front();
    if (!channel->sendWaiters.empty()) {
        sender = channel->sendWaiters.front();
        channel->sendWaiters.pop_front();
    }
    lock.unlock();
    channel->cv_send.notify_one();
    ResumeSchedulerTask(sender);
    return true;
}

extern "C" void absolute_channel_close(void* ch) {
    if (!ch) return;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::deque<Task*> waiters;
    {
        std::lock_guard lock(channel->mutex);
        channel->closed = true;
        waiters.swap(channel->sendWaiters);
        waiters.insert(waiters.end(),
            channel->receiveWaiters.begin(), channel->receiveWaiters.end());
        channel->receiveWaiters.clear();
    }
    channel->cv_recv.notify_all();
    channel->cv_send.notify_all();
    for (Task* waiter : waiters)
        ResumeSchedulerTask(waiter);
}

extern "C" bool absolute_channel_is_closed(void* ch) {
    if (!ch) return true;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    return channel->closed;
}

extern "C" std::int32_t absolute_channel_count(void* ch) {
    if (!ch) return 0;
    ChannelImpl* channel = static_cast<ChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    return static_cast<std::int32_t>(channel->queue.size());
}

extern "C" void absolute_channel_destroy(void* ch) {
    if (!ch) return;
    delete static_cast<ChannelImpl*>(ch);
}

extern "C" void* absolute_transfer_channel_create(std::int32_t capacity) {
    TransferChannelImpl* channel = new TransferChannelImpl();
    channel->capacity = capacity > 0 ? capacity : 0;
    return channel;
}

extern "C" bool absolute_transfer_channel_send(void* ch, void* capsule) {
    if (!ch || !capsule) return false;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    while (true) {
        Task* receiver = nullptr;
        std::unique_lock lock(channel->mutex);
        if (channel->closed) return false;
        if (channel->capacity == 0 ||
            static_cast<std::int32_t>(channel->queue.size()) < channel->capacity) {
            channel->queue.push_back(capsule);
            if (!channel->receiveWaiters.empty()) {
                receiver = channel->receiveWaiters.front();
                channel->receiveWaiters.pop_front();
            }
            lock.unlock();
            channel->cv_recv.notify_one();
            ResumeSchedulerTask(receiver);
            return true;
        }
        if (!currentTask) {
            channel->cv_send.wait(lock, [&] {
                return channel->closed ||
                    static_cast<std::int32_t>(channel->queue.size()) < channel->capacity;
            });
            continue;
        }
        channel->sendWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_transfer_channel_try_send(void* ch, void* capsule) {
    if (!ch || !capsule) return false;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    Task* receiver = nullptr;
    std::unique_lock lock(channel->mutex);
    if (channel->closed) return false;
    if (channel->capacity > 0 &&
        static_cast<std::int32_t>(channel->queue.size()) >= channel->capacity)
        return false;
    channel->queue.push_back(capsule);
    if (!channel->receiveWaiters.empty()) {
        receiver = channel->receiveWaiters.front();
        channel->receiveWaiters.pop_front();
    }
    lock.unlock();
    channel->cv_recv.notify_one();
    ResumeSchedulerTask(receiver);
    return true;
}

extern "C" void* absolute_transfer_channel_receive(void* ch) {
    if (!ch) return nullptr;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    while (true) {
        Task* sender = nullptr;
        std::unique_lock lock(channel->mutex);
        if (!channel->queue.empty()) {
            void* capsule = channel->queue.front();
            channel->queue.pop_front();
            if (!channel->sendWaiters.empty()) {
                sender = channel->sendWaiters.front();
                channel->sendWaiters.pop_front();
            }
            lock.unlock();
            channel->cv_send.notify_one();
            ResumeSchedulerTask(sender);
            return capsule;
        }
        if (channel->closed) return nullptr;
        if (!currentTask) {
            channel->cv_recv.wait(lock, [&] {
                return channel->closed || !channel->queue.empty();
            });
            continue;
        }
        channel->receiveWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" void* absolute_transfer_channel_try_receive(void* ch) {
    if (!ch) return nullptr;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    Task* sender = nullptr;
    std::unique_lock lock(channel->mutex);
    if (channel->queue.empty()) return nullptr;
    void* capsule = channel->queue.front();
    channel->queue.pop_front();
    if (!channel->sendWaiters.empty()) {
        sender = channel->sendWaiters.front();
        channel->sendWaiters.pop_front();
    }
    lock.unlock();
    channel->cv_send.notify_one();
    ResumeSchedulerTask(sender);
    return capsule;
}

extern "C" void absolute_transfer_channel_close(void* ch) {
    if (!ch) return;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    std::deque<Task*> waiters;
    {
        std::lock_guard lock(channel->mutex);
        channel->closed = true;
        waiters.swap(channel->sendWaiters);
        waiters.insert(waiters.end(),
            channel->receiveWaiters.begin(), channel->receiveWaiters.end());
        channel->receiveWaiters.clear();
    }
    channel->cv_recv.notify_all();
    channel->cv_send.notify_all();
    for (Task* waiter : waiters)
        ResumeSchedulerTask(waiter);
}

extern "C" bool absolute_transfer_channel_is_closed(void* ch) {
    if (!ch) return true;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    return channel->closed;
}

extern "C" std::int32_t absolute_transfer_channel_count(void* ch) {
    if (!ch) return 0;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    std::lock_guard lock(channel->mutex);
    return static_cast<std::int32_t>(channel->queue.size());
}

extern "C" void absolute_transfer_channel_destroy(void* ch) {
    if (!ch) return;
    TransferChannelImpl* channel = static_cast<TransferChannelImpl*>(ch);
    for (void* capsule : channel->queue)
        absolute_capsule_destroy(capsule);
    delete channel;
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
    return new MutexImpl();
}

extern "C" void absolute_mutex_lock(void* mutex) {
    if (!mutex) return;
    MutexImpl* state = static_cast<MutexImpl*>(mutex);
    while (true) {
        std::unique_lock lock(state->mutex);
        if (!state->locked) {
            state->locked = true;
            return;
        }
        if (!currentTask) {
            state->available.wait(lock, [&] { return !state->locked; });
            continue;
        }
        state->waiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" void absolute_mutex_unlock(void* mutex) {
    if (!mutex) return;
    MutexImpl* state = static_cast<MutexImpl*>(mutex);
    Task* waiter = nullptr;
    {
        std::lock_guard lock(state->mutex);
        state->locked = false;
        if (!state->waiters.empty()) {
            waiter = state->waiters.front();
            state->waiters.pop_front();
        }
    }
    state->available.notify_one();
    ResumeSchedulerTask(waiter);
}

extern "C" bool absolute_mutex_try_lock(void* mutex) {
    if (!mutex) return false;
    MutexImpl* state = static_cast<MutexImpl*>(mutex);
    std::lock_guard lock(state->mutex);
    if (state->locked) return false;
    state->locked = true;
    return true;
}

extern "C" void absolute_mutex_destroy(void* mutex) {
    if (!mutex) return;
    delete static_cast<MutexImpl*>(mutex);
}
