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
extern "C" void absolute_error_set_details(const char* typeName, const char* message);
extern "C" const char* absolute_error_type_name();
extern "C" const char* absolute_error_message();
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
        // The error state is thread-local and the awaiting thread is not
        // the one that threw, so the description travels with the task.
        std::string errorTypeName;
        std::string errorMessage;
        TaskState state = TaskState::Pending;
        std::shared_ptr<TaskControl> control =
            std::make_shared<TaskControl>();
        TaskClock::time_point queuedAt{};
        TaskClock::time_point suspendedAt{};
        bool suspensionActive = false;
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

    enum SchedulerMetricIndex : std::int32_t {
        SchedulerMetricRunnable = 0,
        SchedulerMetricSuspended = 1,
        SchedulerMetricCompleted = 2,
        SchedulerMetricQueueSamples = 3,
        SchedulerMetricQueueLatencyTotalNanoseconds = 4,
        SchedulerMetricQueueLatencyMaxNanoseconds = 5,
        SchedulerMetricWorkerBusyNanoseconds = 6,
        SchedulerMetricWorkerUtilizationPermille = 7,
        SchedulerMetricSteals = 8,
        SchedulerMetricWakeUps = 9,
        SchedulerMetricBlockedNanoseconds = 10,
        SchedulerMetricStarvationEvents = 11,
        SchedulerMetricCount = 12
    };

    struct SchedulerMetricsSnapshot {
        std::array<std::int64_t, SchedulerMetricCount> values{};
    };

    thread_local int currentCore = -1;
    thread_local int currentPriority = 0;
    thread_local std::string currentRole;
    thread_local bool currentAffinityApplied = false;
    class Scheduler;
    thread_local Scheduler* currentScheduler = nullptr;
    thread_local Task* currentTask = nullptr;
    thread_local FiberContext* currentWorkerFiber = nullptr;
    thread_local int currentWorkerIndex = -1;
    std::atomic<Scheduler*> schedulerInstance{nullptr};

    class AffinityCapabilities {
#if defined(_WIN32)
        std::vector<std::pair<WORD, DWORD>> groups;
#elif defined(__linux__)
        cpu_set_t allowed{};
#endif
        bool supported = false;

    public:
        AffinityCapabilities() {
#if defined(_WIN32)
            const WORD groupCount = GetActiveProcessorGroupCount();
            groups.reserve(groupCount);
            for (WORD group = 0; group < groupCount; ++group) {
                const DWORD count = GetActiveProcessorCount(group);
                if (count > 0) groups.emplace_back(group, count);
            }
            supported = !groups.empty();
#elif defined(__linux__)
            CPU_ZERO(&allowed);
            supported = pthread_getaffinity_np(
                pthread_self(), sizeof(allowed), &allowed) == 0;
#endif
        }

        bool Supported() const { return supported; }

        bool CoreAvailable(int core) const {
            if (!supported || core < 0) return false;
#if defined(_WIN32)
            std::uint64_t remaining = static_cast<std::uint64_t>(core);
            for (const auto& [group, count] : groups) {
                (void)group;
                if (remaining < count) return true;
                remaining -= count;
            }
            return false;
#elif defined(__linux__)
            return core < CPU_SETSIZE && CPU_ISSET(core, &allowed);
#else
            return false;
#endif
        }

#if defined(_WIN32)
        bool ResolveCore(int core, WORD& group, BYTE& processor) const {
            if (!CoreAvailable(core)) return false;
            std::uint64_t remaining = static_cast<std::uint64_t>(core);
            for (const auto& [candidateGroup, count] : groups) {
                if (remaining < count) {
                    group = candidateGroup;
                    processor = static_cast<BYTE>(remaining);
                    return true;
                }
                remaining -= count;
            }
            return false;
        }
#endif
    };

    const AffinityCapabilities& GetAffinityCapabilities() {
        static const AffinityCapabilities capabilities;
        return capabilities;
    }

    class ScopedAffinity {
#if defined(_WIN32)
        GROUP_AFFINITY previous{};
        bool restore = false;
#elif defined(__linux__)
        cpu_set_t previous{};
        bool restore = false;
#endif
        bool applied = false;

    public:
        explicit ScopedAffinity(int core) {
            const AffinityCapabilities& capabilities =
                GetAffinityCapabilities();
            if (!capabilities.CoreAvailable(core)) return;
#if defined(_WIN32)
            WORD group = 0;
            BYTE processor = 0;
            if (!capabilities.ResolveCore(core, group, processor))
                return;
            GROUP_AFFINITY requested{};
            requested.Group = group;
            requested.Mask = KAFFINITY{1} << processor;
            restore = SetThreadGroupAffinity(
                GetCurrentThread(), &requested, &previous) != 0;
            applied = restore;
#elif defined(__linux__)
            if (pthread_getaffinity_np(
                pthread_self(), sizeof(previous), &previous) == 0) {
                cpu_set_t requested;
                CPU_ZERO(&requested);
                CPU_SET(core, &requested);
                restore = pthread_setaffinity_np(
                    pthread_self(), sizeof(requested), &requested) == 0;
                applied = restore;
            }
#else
            (void)core;
#endif
        }

        ~ScopedAffinity() {
#if defined(_WIN32)
            if (restore) {
                GROUP_AFFINITY ignored{};
                SetThreadGroupAffinity(
                    GetCurrentThread(), &previous, &ignored);
            }
#elif defined(__linux__)
            if (restore)
                pthread_setaffinity_np(pthread_self(), sizeof(previous), &previous);
#endif
        }

        bool Applied() const { return applied; }
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
        TaskClock::time_point metricsStarted = TaskClock::now();
        std::atomic<std::int64_t> runnableTasks{0};
        std::atomic<std::int64_t> suspendedTasks{0};
        std::atomic<std::uint64_t> completedTasks{0};
        std::atomic<std::uint64_t> queueSamples{0};
        std::atomic<std::uint64_t> queueLatencyNanoseconds{0};
        std::atomic<std::uint64_t> maximumQueueLatencyNanoseconds{0};
        std::atomic<std::uint64_t> workerBusyNanoseconds{0};
        std::atomic<std::uint64_t> successfulSteals{0};
        std::atomic<std::uint64_t> wakeUps{0};
        std::atomic<std::uint64_t> blockedNanoseconds{0};
        std::atomic<std::uint64_t> starvationEvents{0};
        bool stopping = false;

        static constexpr std::uint64_t starvationThresholdNanoseconds =
            100'000'000;

        static std::uint64_t ElapsedNanoseconds(
            TaskClock::time_point started,
            TaskClock::time_point finished = TaskClock::now()) {
            if (finished <= started) return 0;
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    finished - started).count());
        }

        static void UpdateMaximum(
            std::atomic<std::uint64_t>& target,
            std::uint64_t candidate) {
            std::uint64_t current =
                target.load(std::memory_order_relaxed);
            while (candidate > current &&
                !target.compare_exchange_weak(
                    current, candidate,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {}
        }

        static std::int64_t MetricValue(std::uint64_t value) {
            return static_cast<std::int64_t>(
                std::min<std::uint64_t>(
                    value,
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())));
        }

        void ConsumeQueuedLocked(Task* task) {
            if (queued == 0) std::abort();
            --queued;
            runnableTasks.fetch_sub(1, std::memory_order_relaxed);
            const std::uint64_t latency =
                ElapsedNanoseconds(task->queuedAt);
            queueSamples.fetch_add(1, std::memory_order_relaxed);
            queueLatencyNanoseconds.fetch_add(
                latency, std::memory_order_relaxed);
            UpdateMaximum(maximumQueueLatencyNanoseconds, latency);
            if (latency >= starvationThresholdNanoseconds)
                starvationEvents.fetch_add(
                    1, std::memory_order_relaxed);
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
            ConsumeQueuedLocked(work.task);
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
            work.task->queuedAt = TaskClock::now();
            const size_t bucket =
                static_cast<size_t>(work.priority + 3);
            const std::string role = work.role;
            std::deque<Work>& lane = lanes.lanes[bucket][role];
            if (lane.empty()) lanes.readyRoles[bucket].push_back(role);
            lane.push_back(std::move(work));
            ++lanes.counts[bucket];
            ++queued;
            runnableTasks.fetch_add(1, std::memory_order_relaxed);
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
                if (!task->wakePending) {
                    task->wakePending = true;
                    wakeUps.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else if (task->state == TaskState::Suspended) {
                wakeUps.fetch_add(1, std::memory_order_relaxed);
                if (!task->suspensionActive) std::abort();
                task->suspensionActive = false;
                suspendedTasks.fetch_sub(1, std::memory_order_relaxed);
                blockedNanoseconds.fetch_add(
                    ElapsedNanoseconds(task->suspendedAt),
                    std::memory_order_relaxed);
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
                    task->errorTypeName = absolute_error_type_name();
                    task->errorMessage = absolute_error_message();
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
            const TaskClock::time_point sliceStarted = TaskClock::now();
            const int previousCore = currentCore;
            const int previousPriority = currentPriority;
            std::string previousRole = std::move(currentRole);
            currentCore = work.core;
            currentPriority = work.priority;
            currentRole = work.role;
            currentTask = work.task;
            ScopedAffinity affinity(work.core);
            const bool previousAffinityApplied = currentAffinityApplied;
            currentAffinityApplied = affinity.Applied();

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
                    // Counted before anyone is told the task is done, and under
                    // the same lock. `await` returns the moment it observes
                    // `done`, so counting afterwards left a window in which a
                    // program could await two tasks and then read a metric that
                    // did not include them yet -- which is exactly what
                    // tests/task-scheduling.abs asserts, and what it failed on
                    // under CPU contention: 18 runs in 60 with the machine
                    // busy, and none when it was idle.
                    completedTasks.fetch_add(1, std::memory_order_relaxed);
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
                        work.task->suspendedAt = TaskClock::now();
                        work.task->suspensionActive = true;
                        suspendedTasks.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                }
                if (enqueue) Enqueue(work.task);
            }
            currentTask = nullptr;
            currentCore = previousCore;
            currentPriority = previousPriority;
            currentRole = std::move(previousRole);
            currentAffinityApplied = previousAffinityApplied;
            workerBusyNanoseconds.fetch_add(
                ElapsedNanoseconds(sliceStarted),
                std::memory_order_relaxed);
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
                            const auto deadline = timers.top().deadline;
                            available.wait_until(lock, deadline);
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

        bool AffinitySupported() const {
            return GetAffinityCapabilities().Supported();
        }

        bool CoreAvailable(int core) const {
            return GetAffinityCapabilities().CoreAvailable(core);
        }

        SchedulerMetricsSnapshot* MetricsSnapshot() const {
            auto* snapshot = new SchedulerMetricsSnapshot;
            snapshot->values[SchedulerMetricRunnable] =
                runnableTasks.load(std::memory_order_relaxed);
            snapshot->values[SchedulerMetricSuspended] =
                suspendedTasks.load(std::memory_order_relaxed);
            snapshot->values[SchedulerMetricCompleted] = MetricValue(
                completedTasks.load(std::memory_order_relaxed));
            snapshot->values[SchedulerMetricQueueSamples] = MetricValue(
                queueSamples.load(std::memory_order_relaxed));
            snapshot->values[
                SchedulerMetricQueueLatencyTotalNanoseconds] = MetricValue(
                queueLatencyNanoseconds.load(std::memory_order_relaxed));
            snapshot->values[
                SchedulerMetricQueueLatencyMaxNanoseconds] = MetricValue(
                maximumQueueLatencyNanoseconds.load(
                    std::memory_order_relaxed));
            const std::uint64_t busy =
                workerBusyNanoseconds.load(std::memory_order_relaxed);
            snapshot->values[SchedulerMetricWorkerBusyNanoseconds] =
                MetricValue(busy);
            const std::uint64_t elapsed =
                ElapsedNanoseconds(metricsStarted);
            const long double capacity =
                static_cast<long double>(elapsed) *
                static_cast<long double>(workers.size());
            const long double utilization = capacity > 0.0L
                ? static_cast<long double>(busy) * 1'000.0L / capacity
                : 0.0L;
            snapshot->values[SchedulerMetricWorkerUtilizationPermille] =
                static_cast<std::int64_t>(std::clamp<long double>(
                    utilization, 0.0L, 1'000.0L));
            snapshot->values[SchedulerMetricSteals] = MetricValue(
                successfulSteals.load(std::memory_order_relaxed));
            snapshot->values[SchedulerMetricWakeUps] = MetricValue(
                wakeUps.load(std::memory_order_relaxed));
            snapshot->values[SchedulerMetricBlockedNanoseconds] =
                MetricValue(blockedNanoseconds.load(
                    std::memory_order_relaxed));
            snapshot->values[SchedulerMetricStarvationEvents] =
                MetricValue(starvationEvents.load(
                    std::memory_order_relaxed));
            return snapshot;
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

extern "C" bool absolute_scheduler_affinity_supported() {
    return GetScheduler().AffinitySupported();
}

extern "C" bool absolute_scheduler_core_available(std::int32_t core) {
    return GetScheduler().CoreAvailable(core);
}

extern "C" bool absolute_task_current_affinity_applied() {
    return currentTask && currentAffinityApplied;
}

extern "C" void* absolute_scheduler_metrics_snapshot() {
    return GetScheduler().MetricsSnapshot();
}

extern "C" std::int64_t absolute_scheduler_metrics_value(
    void* snapshotHandle, std::int32_t metric) {
    if (!snapshotHandle || metric < 0 ||
        metric >= SchedulerMetricCount) {
        return 0;
    }
    auto* snapshot =
        static_cast<SchedulerMetricsSnapshot*>(snapshotHandle);
    return snapshot->values[static_cast<size_t>(metric)];
}

extern "C" void absolute_scheduler_metrics_destroy(
    void* snapshotHandle) {
    delete static_cast<SchedulerMetricsSnapshot*>(snapshotHandle);
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
    if (task->errorHandle) {
        absolute_error_set_details(task->errorTypeName.c_str(), task->errorMessage.c_str());
        absolute_error_set(task->errorHandle, task->errorType);
    }
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

    struct SemaphoreImpl {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task*> waiters;
        std::int32_t permits = 0;
        std::int32_t maximum = 1;
    };

    struct RwLockImpl {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task*> readerWaiters;
        std::deque<Task*> writerWaiters;
        std::int32_t readers = 0;
        std::int32_t waitingWriters = 0;
        bool writer = false;
    };

    struct ConditionVariableImpl {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task*> taskWaiters;
        std::uint64_t generation = 0;
        std::int32_t nativeWaiters = 0;
    };

    struct OnceImpl {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Task*> waiters;
        std::int32_t state = 0; // 0 = idle, 1 = running, 2 = complete
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

extern "C" void* absolute_semaphore_create(
    std::int32_t initialPermits, std::int32_t maximumPermits) {
    if (maximumPermits <= 0 || initialPermits < 0 ||
        initialPermits > maximumPermits) {
        return nullptr;
    }
    SemaphoreImpl* semaphore = new SemaphoreImpl();
    semaphore->permits = initialPermits;
    semaphore->maximum = maximumPermits;
    return semaphore;
}

extern "C" void absolute_semaphore_acquire(void* handle) {
    if (!handle) return;
    SemaphoreImpl* semaphore = static_cast<SemaphoreImpl*>(handle);
    while (true) {
        std::unique_lock lock(semaphore->mutex);
        if (semaphore->permits > 0) {
            --semaphore->permits;
            return;
        }
        if (!currentTask) {
            semaphore->available.wait(
                lock, [&] { return semaphore->permits > 0; });
            continue;
        }
        semaphore->waiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_semaphore_try_acquire(void* handle) {
    if (!handle) return false;
    SemaphoreImpl* semaphore = static_cast<SemaphoreImpl*>(handle);
    std::lock_guard lock(semaphore->mutex);
    if (semaphore->permits <= 0) return false;
    --semaphore->permits;
    return true;
}

extern "C" bool absolute_semaphore_release(
    void* handle, std::int32_t permits) {
    if (!handle || permits <= 0) return false;
    SemaphoreImpl* semaphore = static_cast<SemaphoreImpl*>(handle);
    std::vector<Task*> resumed;
    {
        std::lock_guard lock(semaphore->mutex);
        if (permits > semaphore->maximum - semaphore->permits)
            return false;
        semaphore->permits += permits;
        while (permits > 0 && !semaphore->waiters.empty()) {
            resumed.push_back(semaphore->waiters.front());
            semaphore->waiters.pop_front();
            --permits;
        }
    }
    semaphore->available.notify_all();
    for (Task* waiter : resumed) ResumeSchedulerTask(waiter);
    return true;
}

extern "C" std::int32_t absolute_semaphore_available(void* handle) {
    if (!handle) return 0;
    SemaphoreImpl* semaphore = static_cast<SemaphoreImpl*>(handle);
    std::lock_guard lock(semaphore->mutex);
    return semaphore->permits;
}

extern "C" void absolute_semaphore_destroy(void* handle) {
    if (!handle) return;
    delete static_cast<SemaphoreImpl*>(handle);
}

extern "C" void* absolute_rwlock_create() {
    return new RwLockImpl();
}

extern "C" void absolute_rwlock_lock_read(void* handle) {
    if (!handle) return;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    while (true) {
        std::unique_lock lock(rwlock->mutex);
        if (!rwlock->writer && rwlock->waitingWriters == 0) {
            ++rwlock->readers;
            return;
        }
        if (!currentTask) {
            rwlock->available.wait(lock, [&] {
                return !rwlock->writer && rwlock->waitingWriters == 0;
            });
            continue;
        }
        rwlock->readerWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_rwlock_try_lock_read(void* handle) {
    if (!handle) return false;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    std::lock_guard lock(rwlock->mutex);
    if (rwlock->writer || rwlock->waitingWriters > 0) return false;
    ++rwlock->readers;
    return true;
}

extern "C" void absolute_rwlock_unlock_read(void* handle) {
    if (!handle) return;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    Task* writer = nullptr;
    {
        std::lock_guard lock(rwlock->mutex);
        if (rwlock->readers <= 0) return;
        --rwlock->readers;
        if (rwlock->readers == 0 && !rwlock->writerWaiters.empty()) {
            writer = rwlock->writerWaiters.front();
            rwlock->writerWaiters.pop_front();
        }
    }
    rwlock->available.notify_all();
    ResumeSchedulerTask(writer);
}

extern "C" void absolute_rwlock_lock_write(void* handle) {
    if (!handle) return;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    bool registered = false;
    while (true) {
        std::unique_lock lock(rwlock->mutex);
        if (!registered) {
            ++rwlock->waitingWriters;
            registered = true;
        }
        if (!rwlock->writer && rwlock->readers == 0) {
            --rwlock->waitingWriters;
            rwlock->writer = true;
            return;
        }
        if (!currentTask) {
            rwlock->available.wait(lock, [&] {
                return !rwlock->writer && rwlock->readers == 0;
            });
            continue;
        }
        rwlock->writerWaiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" bool absolute_rwlock_try_lock_write(void* handle) {
    if (!handle) return false;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    std::lock_guard lock(rwlock->mutex);
    if (rwlock->writer || rwlock->readers != 0) return false;
    rwlock->writer = true;
    return true;
}

extern "C" void absolute_rwlock_unlock_write(void* handle) {
    if (!handle) return;
    RwLockImpl* rwlock = static_cast<RwLockImpl*>(handle);
    Task* writer = nullptr;
    std::vector<Task*> readers;
    {
        std::lock_guard lock(rwlock->mutex);
        if (!rwlock->writer) return;
        rwlock->writer = false;
        if (!rwlock->writerWaiters.empty()) {
            writer = rwlock->writerWaiters.front();
            rwlock->writerWaiters.pop_front();
        } else {
            while (!rwlock->readerWaiters.empty()) {
                readers.push_back(rwlock->readerWaiters.front());
                rwlock->readerWaiters.pop_front();
            }
        }
    }
    rwlock->available.notify_all();
    ResumeSchedulerTask(writer);
    for (Task* reader : readers) ResumeSchedulerTask(reader);
}

extern "C" void absolute_rwlock_destroy(void* handle) {
    if (!handle) return;
    delete static_cast<RwLockImpl*>(handle);
}

extern "C" void* absolute_condition_create() {
    return new ConditionVariableImpl();
}

extern "C" void absolute_condition_wait(
    void* conditionHandle, void* mutexHandle) {
    if (!conditionHandle || !mutexHandle) return;
    ConditionVariableImpl* condition =
        static_cast<ConditionVariableImpl*>(conditionHandle);
    if (currentTask) {
        {
            std::lock_guard lock(condition->mutex);
            condition->taskWaiters.push_back(currentTask);
            currentScheduler->PrepareSuspend();
            absolute_mutex_unlock(mutexHandle);
        }
        currentScheduler->YieldCurrent();
        absolute_mutex_lock(mutexHandle);
        return;
    }
    std::unique_lock lock(condition->mutex);
    const std::uint64_t observed = condition->generation;
    ++condition->nativeWaiters;
    absolute_mutex_unlock(mutexHandle);
    condition->available.wait(
        lock, [&] { return condition->generation != observed; });
    --condition->nativeWaiters;
    lock.unlock();
    absolute_mutex_lock(mutexHandle);
}

extern "C" void absolute_condition_notify_one(void* handle) {
    if (!handle) return;
    ConditionVariableImpl* condition =
        static_cast<ConditionVariableImpl*>(handle);
    Task* waiter = nullptr;
    bool notifyNative = false;
    {
        std::lock_guard lock(condition->mutex);
        if (!condition->taskWaiters.empty()) {
            waiter = condition->taskWaiters.front();
            condition->taskWaiters.pop_front();
        } else if (condition->nativeWaiters > 0) {
            ++condition->generation;
            notifyNative = true;
        }
    }
    if (notifyNative) condition->available.notify_one();
    ResumeSchedulerTask(waiter);
}

extern "C" void absolute_condition_notify_all(void* handle) {
    if (!handle) return;
    ConditionVariableImpl* condition =
        static_cast<ConditionVariableImpl*>(handle);
    std::vector<Task*> waiters;
    bool notifyNative = false;
    {
        std::lock_guard lock(condition->mutex);
        while (!condition->taskWaiters.empty()) {
            waiters.push_back(condition->taskWaiters.front());
            condition->taskWaiters.pop_front();
        }
        if (condition->nativeWaiters > 0) {
            ++condition->generation;
            notifyNative = true;
        }
    }
    if (notifyNative) condition->available.notify_all();
    for (Task* waiter : waiters) ResumeSchedulerTask(waiter);
}

extern "C" void absolute_condition_destroy(void* handle) {
    if (!handle) return;
    delete static_cast<ConditionVariableImpl*>(handle);
}

extern "C" void* absolute_once_create() {
    return new OnceImpl();
}

extern "C" bool absolute_once_begin(void* handle) {
    if (!handle) return false;
    OnceImpl* once = static_cast<OnceImpl*>(handle);
    while (true) {
        std::unique_lock lock(once->mutex);
        if (once->state == 2) return false;
        if (once->state == 0) {
            once->state = 1;
            return true;
        }
        if (!currentTask) {
            once->available.wait(lock, [&] { return once->state != 1; });
            continue;
        }
        once->waiters.push_back(currentTask);
        currentScheduler->PrepareSuspend();
        lock.unlock();
        currentScheduler->YieldCurrent();
    }
}

extern "C" void absolute_once_complete(void* handle) {
    if (!handle) return;
    OnceImpl* once = static_cast<OnceImpl*>(handle);
    std::vector<Task*> waiters;
    {
        std::lock_guard lock(once->mutex);
        if (once->state != 1) return;
        once->state = 2;
        while (!once->waiters.empty()) {
            waiters.push_back(once->waiters.front());
            once->waiters.pop_front();
        }
    }
    once->available.notify_all();
    for (Task* waiter : waiters) ResumeSchedulerTask(waiter);
}

extern "C" void absolute_once_reset(void* handle) {
    if (!handle) return;
    OnceImpl* once = static_cast<OnceImpl*>(handle);
    std::vector<Task*> waiters;
    {
        std::lock_guard lock(once->mutex);
        if (once->state != 1) return;
        once->state = 0;
        while (!once->waiters.empty()) {
            waiters.push_back(once->waiters.front());
            once->waiters.pop_front();
        }
    }
    once->available.notify_all();
    for (Task* waiter : waiters) ResumeSchedulerTask(waiter);
}

extern "C" bool absolute_once_is_complete(void* handle) {
    if (!handle) return false;
    OnceImpl* once = static_cast<OnceImpl*>(handle);
    std::lock_guard lock(once->mutex);
    return once->state == 2;
}

extern "C" void absolute_once_destroy(void* handle) {
    if (!handle) return;
    delete static_cast<OnceImpl*>(handle);
}
