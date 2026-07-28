#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "scheduler_fiber.h"
#include "scheduler_io.h"

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

    using TaskClock = std::chrono::steady_clock;

    struct TaskControl {
        std::shared_ptr<TaskControl> parent;
        std::atomic<bool> cancelRequested{false};
        std::atomic<std::int64_t> deadlineNanoseconds{0};
    };

    std::int64_t TaskNowNanoseconds() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            TaskClock::now().time_since_epoch()).count();
    }

    std::int64_t EffectiveDeadlineNanoseconds(
        const std::shared_ptr<TaskControl>& control) {
        std::int64_t effective = 0;
        for (auto current = control; current; current = current->parent) {
            const std::int64_t candidate =
                current->deadlineNanoseconds.load(std::memory_order_acquire);
            if (candidate > 0 && (effective == 0 || candidate < effective))
                effective = candidate;
        }
        return effective;
    }

    bool ControlCancelled(const std::shared_ptr<TaskControl>& control) {
        for (auto current = control; current; current = current->parent) {
            if (current->cancelRequested.load(std::memory_order_acquire))
                return true;
        }
        const std::int64_t deadline =
            EffectiveDeadlineNanoseconds(control);
        return deadline > 0 && deadline <= TaskNowNanoseconds();
    }

    bool SetControlDeadlineAfter(
        const std::shared_ptr<TaskControl>& control, std::int32_t milliseconds) {
        if (!control || milliseconds < 0) return false;
        const std::int64_t now = TaskNowNanoseconds();
        const std::int64_t maximum =
            std::numeric_limits<std::int64_t>::max();
        const std::int64_t duration =
            static_cast<std::int64_t>(milliseconds) * 1'000'000;
        const std::int64_t deadline =
            duration > maximum - now ? maximum : now + duration;
        std::int64_t current =
            control->deadlineNanoseconds.load(std::memory_order_acquire);
        while ((current == 0 || deadline < current) &&
            !control->deadlineNanoseconds.compare_exchange_weak(
                current, deadline,
                std::memory_order_release, std::memory_order_acquire)) {}
        return true;
    }

    std::int32_t RemainingDeadlineMilliseconds(
        const std::shared_ptr<TaskControl>& control) {
        const std::int64_t deadline =
            EffectiveDeadlineNanoseconds(control);
        if (deadline == 0) return -1;
        const std::int64_t remaining = deadline - TaskNowNanoseconds();
        if (remaining <= 0) return 0;
        const std::int64_t milliseconds =
            (remaining + 999'999) / 1'000'000;
        return static_cast<std::int32_t>(std::min<std::int64_t>(
            milliseconds, std::numeric_limits<std::int32_t>::max()));
    }

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
        std::shared_ptr<TaskControl> control =
            std::make_shared<TaskControl>();
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
        struct LaneSet {
            std::array<
                std::unordered_map<std::string, std::deque<Work>>, 7> lanes;
            std::array<std::deque<std::string>, 7> readyRoles;
            std::array<size_t, 7> counts{};
        };
        struct WorkerQueue {
            LaneSet ready;
            LaneSet pinned;
            std::array<std::int64_t, 7> localCredits{};
            std::array<std::int64_t, 7> stealCredits{};
            std::array<bool, 7> takePinnedNext{};
        };
        static constexpr std::array<std::int64_t, 7> priorityWeights{
            1, 2, 3, 4, 6, 8, 12};
        std::vector<WorkerQueue> queues;
        std::vector<size_t> stealCursors;
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
        size_t nextSubmissionQueue = 0;
        std::uint64_t nextTimerSequence = 0;
        std::atomic<std::uint64_t> successfulSteals{0};
        bool stopping = false;

        void ConsumeQueuedLocked() {
            if (queued == 0) std::abort();
            --queued;
            if (stopping && queued == 0) available.notify_all();
        }

        int ChoosePriorityLocked(
            std::array<std::int64_t, 7>& credits,
            const LaneSet& first, const LaneSet* second = nullptr) {
            std::int64_t activeWeight = 0;
            std::int64_t bestCredit =
                std::numeric_limits<std::int64_t>::min();
            int selected = -1;
            for (size_t index = 0; index < priorityWeights.size(); ++index) {
                const bool isActive =
                    first.counts[index] > 0 ||
                    (second && second->counts[index] > 0);
                if (!isActive) {
                    credits[index] = 0;
                    continue;
                }
                activeWeight += priorityWeights[index];
                credits[index] += priorityWeights[index];
                if (credits[index] > bestCredit ||
                    (credits[index] == bestCredit &&
                        static_cast<int>(index) > selected)) {
                    bestCredit = credits[index];
                    selected = static_cast<int>(index);
                }
            }
            if (selected >= 0)
                credits[static_cast<size_t>(selected)] -= activeWeight;
            return selected;
        }

        bool TakeLaneLocked(
            LaneSet& lanes, size_t bucket, Work& work) {
            if (lanes.counts[bucket] == 0) return false;
            if (lanes.readyRoles[bucket].empty()) std::abort();
            std::string role =
                std::move(lanes.readyRoles[bucket].front());
            lanes.readyRoles[bucket].pop_front();
            auto lane = lanes.lanes[bucket].find(role);
            if (lane == lanes.lanes[bucket].end() || lane->second.empty())
                std::abort();
            work = std::move(lane->second.front());
            lane->second.pop_front();
            --lanes.counts[bucket];
            ConsumeQueuedLocked();
            if (lane->second.empty()) lanes.lanes[bucket].erase(lane);
            else lanes.readyRoles[bucket].push_back(std::move(role));
            return true;
        }

        bool TakeLocalLocked(WorkerQueue& queue, Work& work) {
            const int selected = ChoosePriorityLocked(
                queue.localCredits, queue.ready, &queue.pinned);
            if (selected < 0) return false;
            const size_t bucket = static_cast<size_t>(selected);
            const bool hasReady = queue.ready.counts[bucket] > 0;
            const bool hasPinned = queue.pinned.counts[bucket] > 0;
            const bool usePinned =
                hasPinned && (!hasReady || queue.takePinnedNext[bucket]);
            if (hasReady && hasPinned)
                queue.takePinnedNext[bucket] = !usePinned;
            return TakeLaneLocked(
                usePinned ? queue.pinned : queue.ready, bucket, work);
        }

        bool TakeStealableLocked(WorkerQueue& queue, Work& work) {
            const int selected = ChoosePriorityLocked(
                queue.stealCredits, queue.ready);
            return selected >= 0 && TakeLaneLocked(
                queue.ready, static_cast<size_t>(selected), work);
        }

        bool TakeLocked(Work& work, int workerIndex) {
            if (queued == 0) return false;
            const size_t localIndex = static_cast<size_t>(workerIndex);
            WorkerQueue& local = queues[localIndex];
            if (TakeLocalLocked(local, work)) return true;

            const size_t count = queues.size();
            if (count < 2) return false;
            size_t victim = stealCursors[localIndex] % count;
            for (size_t checked = 0; checked < count; ++checked) {
                if (victim != localIndex &&
                    TakeStealableLocked(queues[victim], work)) {
                    stealCursors[localIndex] = (victim + 1) % count;
                    successfulSteals.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                victim = (victim + 1) % count;
            }
            stealCursors[localIndex] = victim;
            return false;
        }

        void EnqueueLaneLocked(LaneSet& lanes, Work work) {
            const size_t bucket =
                static_cast<size_t>(work.priority + 3);
            const std::string role = work.role;
            std::deque<Work>& lane = lanes.lanes[bucket][role];
            if (lane.empty()) lanes.readyRoles[bucket].push_back(role);
            lane.push_back(std::move(work));
            ++lanes.counts[bucket];
            ++queued;
        }

        void EnqueueReadyLocked(size_t queueIndex, Work work) {
            EnqueueLaneLocked(
                queues[queueIndex].ready, std::move(work));
        }

        void EnqueueLocked(Task* task) {
            if (task->ownerWorker >= 0) {
                EnqueueLaneLocked(
                    queues[static_cast<size_t>(task->ownerWorker)].pinned, {
                    task, task->entry, task->core, task->priority, task->role});
                return;
            }
            size_t queueIndex = nextSubmissionQueue++ % queues.size();
            if (currentScheduler == this && currentWorkerIndex >= 0)
                queueIndex = static_cast<size_t>(currentWorkerIndex);
            EnqueueReadyLocked(queueIndex, {
                task, task->entry, task->core, task->priority, task->role});
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
            queues.resize(count);
            stealCursors.resize(count);
            for (unsigned index = 0; index < count; ++index)
                stealCursors[index] = (index + 1) % count;
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
                size_t queueIndex =
                    nextSubmissionQueue++ % queues.size();
                if (currentScheduler == this && currentWorkerIndex >= 0)
                    queueIndex = static_cast<size_t>(currentWorkerIndex);
                EnqueueReadyLocked(queueIndex, {
                    task, entry, core, priority, std::move(role)});
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

        std::uint64_t StealCount() const {
            return successfulSteals.load(std::memory_order_relaxed);
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

    struct BlockingIoCall {
        Absolute::RuntimeDetail::BlockingIoOperation operation = nullptr;
        void* context = nullptr;
        Task* task = nullptr;
        std::exception_ptr exception;
    };

    class BlockingIoExecutor {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<BlockingIoCall*> queued;
        std::vector<std::thread> workers;
        bool stopping = false;

        static unsigned ResolveWorkerCount() {
            constexpr unsigned minimumWorkers = 2;
            constexpr unsigned maximumWorkers = 32;
            if (const char* configured = std::getenv("ABSOLUTE_IO_WORKERS")) {
                char* end = nullptr;
                const unsigned long value = std::strtoul(configured, &end, 10);
                if (end != configured && *end == '\0' &&
                    value >= minimumWorkers && value <= maximumWorkers) {
                    return static_cast<unsigned>(value);
                }
                std::cerr << "Absolute runtime warning: ignoring invalid "
                    "ABSOLUTE_IO_WORKERS='" << configured << "'\n";
            }
            const unsigned hardware = std::thread::hardware_concurrency();
            return std::min(maximumWorkers, std::max(minimumWorkers,
                std::min(4U, std::max(1U, hardware))));
        }

        void Run() {
            while (true) {
                BlockingIoCall* call = nullptr;
                {
                    std::unique_lock lock(mutex);
                    available.wait(lock, [&] {
                        return stopping || !queued.empty();
                    });
                    if (stopping && queued.empty()) return;
                    call = queued.front();
                    queued.pop_front();
                }
                try {
                    call->operation(call->context);
                }
                catch (...) {
                    call->exception = std::current_exception();
                }
                // Resume is deliberately the final access to call. Its storage
                // belongs to the suspended fiber and may disappear immediately.
                ResumeSchedulerTask(call->task);
            }
        }

    public:
        BlockingIoExecutor() {
            const unsigned count = ResolveWorkerCount();
            workers.reserve(count);
            for (unsigned index = 0; index < count; ++index)
                workers.emplace_back([this] { Run(); });
        }

        ~BlockingIoExecutor() {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            available.notify_all();
            for (std::thread& worker : workers)
                if (worker.joinable()) worker.join();
        }

        void Submit(BlockingIoCall* call) {
            {
                std::lock_guard lock(mutex);
                queued.push_back(call);
            }
            available.notify_one();
        }
    };

    BlockingIoExecutor& GetBlockingIoExecutor() {
        static BlockingIoExecutor executor;
        return executor;
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
    if (currentTask) task->control->parent = currentTask->control;
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

extern "C" std::uint64_t absolute_scheduler_steal_count() {
    return GetScheduler().StealCount();
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

    struct TaskGroupImpl {
        std::mutex mutex;
        std::vector<Task*> children;
        bool closed = false;
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
    auto deadline = TaskClock::now() +
        std::chrono::milliseconds(ms);
    if (currentTask) {
        const std::int64_t taskDeadline =
            EffectiveDeadlineNanoseconds(currentTask->control);
        if (taskDeadline > 0) {
            const auto inherited = TaskClock::time_point(
                std::chrono::nanoseconds(taskDeadline));
            if (inherited < deadline) deadline = inherited;
        }
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

namespace Absolute::RuntimeDetail {
    bool IsSchedulerTask() noexcept {
        return currentTask != nullptr;
    }

    std::int32_t CurrentTaskDeadlineMilliseconds() noexcept {
        return currentTask
            ? RemainingDeadlineMilliseconds(currentTask->control) : -1;
    }

    void RunBlockingIoImpl(BlockingIoOperation operation, void* context) {
        if (!operation) return;
        if (!currentTask) {
            operation(context);
            return;
        }

        BlockingIoCall call{operation, context, currentTask, {}};
        currentScheduler->PrepareSuspend();
        GetBlockingIoExecutor().Submit(&call);
        currentScheduler->YieldCurrent();
        if (call.exception) std::rethrow_exception(call.exception);
    }

    void SuspendForIo(IoRegistration registration, void* context) {
        if (!registration || !currentTask || !currentScheduler) {
            std::cerr << "Absolute runtime error: native I/O suspension "
                "requires a running scheduler task\n";
            std::abort();
        }

        struct Suspension {
            Task* task = nullptr;
        } suspension{currentTask};

        currentScheduler->PrepareSuspend();
        registration(
            context,
            [](void* opaque) {
                auto* value = static_cast<Suspension*>(opaque);
                // Completion is the final access: the resumed fiber owns this
                // stack storage and may return from SuspendForIo immediately.
                ResumeSchedulerTask(value->task);
            },
            &suspension);
        currentScheduler->YieldCurrent();
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

extern "C" void absolute_task_cancel(void* handle) {
    if (!handle) return;
    static_cast<Task*>(handle)->control->cancelRequested.store(
        true, std::memory_order_release);
    NotifySchedulerProgress();
}

extern "C" bool absolute_task_current_cancelled() {
    return currentTask && ControlCancelled(currentTask->control);
}

extern "C" bool absolute_task_current_set_deadline_after(
    std::int32_t milliseconds) {
    return currentTask &&
        SetControlDeadlineAfter(currentTask->control, milliseconds);
}

extern "C" std::int32_t absolute_task_current_deadline_remaining() {
    return currentTask
        ? RemainingDeadlineMilliseconds(currentTask->control) : -1;
}

extern "C" void* absolute_task_group_create() {
    return new TaskGroupImpl();
}

extern "C" bool absolute_task_group_add(void* handle, void* childHandle) {
    if (!childHandle) return false;
    Task* child = static_cast<Task*>(childHandle);
    TaskGroupImpl* group = static_cast<TaskGroupImpl*>(handle);
    if (!group) {
        absolute_task_cancel(child);
        absolute_task_destroy(child);
        return false;
    }

    {
        std::lock_guard lock(group->mutex);
        if (!group->closed) {
            group->children.push_back(child);
            return true;
        }
    }

    // Ownership transfers on every add attempt. A closed group rejects the
    // child but still cancels and joins it, so no detached task can escape.
    absolute_task_cancel(child);
    absolute_task_destroy(child);
    return false;
}

extern "C" std::int32_t absolute_task_group_count(void* handle) {
    TaskGroupImpl* group = static_cast<TaskGroupImpl*>(handle);
    if (!group) return 0;
    std::lock_guard lock(group->mutex);
    return static_cast<std::int32_t>(std::min<std::size_t>(
        group->children.size(), INT32_MAX));
}

extern "C" void absolute_task_group_cancel(void* handle) {
    TaskGroupImpl* group = static_cast<TaskGroupImpl*>(handle);
    if (!group) return;
    std::lock_guard lock(group->mutex);
    for (Task* child : group->children)
        child->control->cancelRequested.store(
            true, std::memory_order_release);
    NotifySchedulerProgress();
}

extern "C" void absolute_task_group_join(void* handle) {
    TaskGroupImpl* group = static_cast<TaskGroupImpl*>(handle);
    if (!group) return;
    std::vector<Task*> children;
    {
        std::lock_guard lock(group->mutex);
        group->closed = true;
        children.swap(group->children);
    }
    for (Task* child : children)
        absolute_task_destroy(child);
}

extern "C" void absolute_task_group_cancel_and_join(void* handle) {
    absolute_task_group_cancel(handle);
    absolute_task_group_join(handle);
}

extern "C" void absolute_task_group_destroy(void* handle) {
    TaskGroupImpl* group = static_cast<TaskGroupImpl*>(handle);
    if (!group) return;
    absolute_task_group_cancel_and_join(group);
    delete group;
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
