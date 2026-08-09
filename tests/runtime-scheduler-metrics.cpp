#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
void absolute_task_delay(std::int32_t milliseconds);
std::int32_t absolute_scheduler_worker_count();
void* absolute_scheduler_metrics_snapshot();
std::int64_t absolute_scheduler_metrics_value(
    void* snapshot, std::int32_t metric);
void absolute_scheduler_metrics_destroy(void* snapshot);
}

namespace {
enum Metric : std::int32_t {
    Runnable = 0,
    Suspended = 1,
    Completed = 2,
    QueueSamples = 3,
    QueueLatencyTotalNanoseconds = 4,
    QueueLatencyMaxNanoseconds = 5,
    WorkerBusyNanoseconds = 6,
    WorkerUtilizationPermille = 7,
    Steals = 8,
    WakeUps = 9,
    BlockedNanoseconds = 10,
    StarvationEvents = 11
};

// Name the failing check. A bare abort() leaves a CI failure with an empty
// log and nothing to diagnose.
void requireAt(bool condition, int line, const char* text) {
    if (condition) return;
    std::cerr << "runtime-scheduler-metrics: check failed at line " << line
        << ": " << text << '\n';
    std::cerr.flush();
    std::abort();
}

#define require(condition) requireAt((condition), __LINE__, #condition)

struct Metrics {
    std::int64_t runnable = 0;
    std::int64_t suspended = 0;
    std::int64_t completed = 0;
    std::int64_t queueSamples = 0;
    std::int64_t queueLatencyTotalNanoseconds = 0;
    std::int64_t queueLatencyMaxNanoseconds = 0;
    std::int64_t workerBusyNanoseconds = 0;
    std::int64_t workerUtilizationPermille = 0;
    std::int64_t steals = 0;
    std::int64_t wakeUps = 0;
    std::int64_t blockedNanoseconds = 0;
    std::int64_t starvationEvents = 0;
};

Metrics snapshot() {
    void* handle = absolute_scheduler_metrics_snapshot();
    require(handle != nullptr);
    Metrics result{
        absolute_scheduler_metrics_value(handle, Runnable),
        absolute_scheduler_metrics_value(handle, Suspended),
        absolute_scheduler_metrics_value(handle, Completed),
        absolute_scheduler_metrics_value(handle, QueueSamples),
        absolute_scheduler_metrics_value(
            handle, QueueLatencyTotalNanoseconds),
        absolute_scheduler_metrics_value(
            handle, QueueLatencyMaxNanoseconds),
        absolute_scheduler_metrics_value(handle, WorkerBusyNanoseconds),
        absolute_scheduler_metrics_value(
            handle, WorkerUtilizationPermille),
        absolute_scheduler_metrics_value(handle, Steals),
        absolute_scheduler_metrics_value(handle, WakeUps),
        absolute_scheduler_metrics_value(handle, BlockedNanoseconds),
        absolute_scheduler_metrics_value(handle, StarvationEvents)};
    require(absolute_scheduler_metrics_value(handle, -1) == 0);
    require(absolute_scheduler_metrics_value(handle, 12) == 0);
    absolute_scheduler_metrics_destroy(handle);
    return result;
}

// Worker threads publish these counters, so a metric can lag the effect it
// describes by a scheduling quantum, and a loaded machine gives no guarantee
// that this thread runs inside any particular window. Wait for the expected
// state rather than sampling once.
template <class Predicate>
Metrics waitForMetrics(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    Metrics current = snapshot();
    while (!predicate(current) &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        current = snapshot();
    }
    return current;
}

struct BlockingContext {
    std::atomic<bool> started{false};
};

void blockingWorker(void* opaque) {
    auto* context = static_cast<BlockingContext*>(opaque);
    context->started.store(true, std::memory_order_release);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now() < deadline)
        std::atomic_signal_fence(std::memory_order_seq_cst);
}

struct ProbeContext {
    bool ran = false;
};

void queuedProbe(void* opaque) {
    static_cast<ProbeContext*>(opaque)->ran = true;
}

struct DelayContext {
    std::atomic<bool> started{false};
    bool finished = false;
};

void suspendedWorker(void* opaque) {
    auto* context = static_cast<DelayContext*>(opaque);
    context->started.store(true, std::memory_order_release);
    absolute_task_delay(250);
    context->finished = true;
}

template <class T>
T* await(void* task) {
    return static_cast<T*>(absolute_task_await(task));
}
}  // namespace

int main() {
#if defined(_WIN32)
    _putenv_s("ABSOLUTE_SCHEDULER_WORKERS", "1");
#else
    setenv("ABSOLUTE_SCHEDULER_WORKERS", "1", 1);
#endif

    require(absolute_scheduler_worker_count() == 1);
    const Metrics initial = snapshot();
    require(initial.runnable == 0);
    require(initial.suspended == 0);

    auto* blocking = new BlockingContext;
    void* blockingTask = absolute_task_spawn_config(
        blockingWorker, blocking, -1, 3, "metrics-blocking");
    while (!blocking->started.load(std::memory_order_acquire))
        std::this_thread::yield();

    auto* probe = new ProbeContext;
    void* probeTask = absolute_task_spawn_config(
        queuedProbe, probe, -1, -3, "metrics-starved");
    const Metrics queued = snapshot();
    require(queued.runnable >= 1);

    blocking = await<BlockingContext>(blockingTask);
    probe = await<ProbeContext>(probeTask);
    require(probe->ran);
    delete blocking;
    delete probe;

    auto* delayed = new DelayContext;
    void* delayedTask = absolute_task_spawn_config(
        suspendedWorker, delayed, -1, 0, "metrics-suspended");
    while (!delayed->started.load(std::memory_order_acquire))
        std::this_thread::yield();

    const Metrics suspendedView = waitForMetrics(
        [](const Metrics& value) { return value.suspended >= 1; },
        std::chrono::seconds(5));
    require(suspendedView.suspended >= 1);
    delayed = await<DelayContext>(delayedTask);
    require(delayed->finished);
    delete delayed;

    const Metrics final = waitForMetrics(
        [&](const Metrics& value) {
            return value.runnable == 0 && value.suspended == 0 &&
                value.completed - initial.completed >= 3 &&
                value.queueSamples - initial.queueSamples >= 3;
        },
        std::chrono::seconds(5));
    require(final.runnable == 0);
    require(final.suspended == 0);
    require(final.completed - initial.completed >= 3);
    require(final.queueSamples - initial.queueSamples >= 3);
    require(final.queueLatencyTotalNanoseconds >=
        final.queueLatencyMaxNanoseconds);
    require(final.queueLatencyMaxNanoseconds >= 100'000'000);
    require(final.workerBusyNanoseconds -
        initial.workerBusyNanoseconds >= 100'000'000);
    require(final.workerUtilizationPermille > 0);
    require(final.workerUtilizationPermille <= 1'000);
    require(final.wakeUps - initial.wakeUps >= 1);
    require(final.blockedNanoseconds -
        initial.blockedNanoseconds >= 10'000'000);
    require(final.starvationEvents -
        initial.starvationEvents >= 1);

    std::cout << "runtime-scheduler-metrics=ok\n";
    return 0;
}
