#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
bool absolute_task_await_timeout(void* handle, std::int32_t milliseconds);
void absolute_task_destroy(void* handle);
void absolute_task_cancel(void* handle);
void absolute_task_delay(std::int32_t milliseconds);
bool absolute_task_current_cancelled();
std::int32_t absolute_scheduler_worker_count();

void* absolute_task_group_create();
bool absolute_task_group_add(void* group, void* child);
void absolute_task_group_join(void* group);
void absolute_task_group_cancel_and_join(void* group);
void absolute_task_group_destroy(void* group);

void* absolute_channel_create(std::int32_t capacity);
bool absolute_channel_send(void* channel, std::int64_t value);
bool absolute_channel_receive_checked(void* channel, std::int64_t* value);
void absolute_channel_close(void* channel);
bool absolute_channel_is_closed(void* channel);
void absolute_channel_destroy(void* channel);
}

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}

template <class Context>
Context* await(void* task) {
    return static_cast<Context*>(absolute_task_await(task));
}

struct StormChildContext {
    std::int64_t value = 0;
};

void stormChild(void* opaque) {
    auto* context = static_cast<StormChildContext*>(opaque);
    context->value = context->value * 3 + 1;
}

struct StormParentContext {
    std::int32_t childCount = 0;
    std::int64_t checksum = 0;
};

void stormParent(void* opaque) {
    auto* context = static_cast<StormParentContext*>(opaque);
    for (std::int32_t index = 0; index < context->childCount; ++index) {
        auto* child = new StormChildContext{index};
        void* task = absolute_task_spawn_config(
            stormChild, child, -1, index % 7 - 3, "storm-child");
        child = await<StormChildContext>(task);
        context->checksum += child->value;
        delete child;
    }
}

void runSpawnAwaitStorm() {
    constexpr std::int32_t parentCount = 32;
    constexpr std::int32_t childrenPerParent = 32;
    std::vector<void*> tasks;
    tasks.reserve(parentCount);
    for (std::int32_t index = 0; index < parentCount; ++index) {
        tasks.push_back(absolute_task_spawn_config(
            stormParent,
            new StormParentContext{childrenPerParent, 0},
            -1, index % 7 - 3, "storm-parent"));
    }

    const std::int64_t expectedPerParent =
        3LL * childrenPerParent * (childrenPerParent - 1) / 2 +
        childrenPerParent;
    for (void* task : tasks) {
        auto* context = await<StormParentContext>(task);
        require(context->checksum == expectedPerParent);
        delete context;
    }
}

struct CancellationRaceContext {
    std::atomic<std::int32_t>* started = nullptr;
    std::atomic<std::int32_t>* completed = nullptr;
    std::atomic<std::int32_t>* observed = nullptr;
    std::int32_t index = 0;
};

void cancellationRaceTask(void* opaque) {
    auto* context = static_cast<CancellationRaceContext*>(opaque);
    context->started->fetch_add(1, std::memory_order_release);
    const std::int32_t steps = context->index % 4 == 0 ? 1 : 8;
    for (std::int32_t step = 0; step < steps; ++step) {
        if (absolute_task_current_cancelled()) {
            context->observed->fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (steps > 1) absolute_task_delay(1);
    }
    context->completed->fetch_add(1, std::memory_order_relaxed);
}

void runCancelVsComplete() {
    constexpr std::int32_t taskCount = 256;
    std::atomic<std::int32_t> started{0};
    std::atomic<std::int32_t> completed{0};
    std::atomic<std::int32_t> observed{0};
    std::vector<void*> tasks;
    tasks.reserve(taskCount);
    for (std::int32_t index = 0; index < taskCount; ++index) {
        tasks.push_back(absolute_task_spawn_config(
            cancellationRaceTask,
            new CancellationRaceContext{
                &started, &completed, &observed, index},
            -1, index % 7 - 3, "cancel-race"));
    }

    std::thread canceller([&] {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (started.load(std::memory_order_acquire) == 0 &&
            std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        for (void* task : tasks) absolute_task_cancel(task);
    });
    canceller.join();

    for (void* task : tasks)
        delete await<CancellationRaceContext>(task);
    require(started.load(std::memory_order_relaxed) == taskCount);
    require(completed.load(std::memory_order_relaxed) == taskCount);
    require(observed.load(std::memory_order_relaxed) > 0);
}

struct DestroyWaitContext {
    std::atomic<std::int32_t>* completed = nullptr;
    std::int32_t delay = 0;
};

void destroyWaitTask(void* opaque) {
    auto* context = static_cast<DestroyWaitContext*>(opaque);
    absolute_task_delay(context->delay);
    context->completed->fetch_add(1, std::memory_order_relaxed);
}

void runDestroyVsWait() {
    constexpr std::int32_t taskCount = 256;
    constexpr std::int32_t reaperCount = 8;
    std::atomic<std::int32_t> completed{0};
    std::vector<void*> tasks;
    tasks.reserve(taskCount);
    for (std::int32_t index = 0; index < taskCount; ++index) {
        auto* context = static_cast<DestroyWaitContext*>(
            std::malloc(sizeof(DestroyWaitContext)));
        require(context != nullptr);
        context->completed = &completed;
        context->delay = index % 4;
        tasks.push_back(absolute_task_spawn_config(
            destroyWaitTask, context, -1, index % 7 - 3, "destroy-wait"));
    }

    std::vector<std::thread> reapers;
    for (std::int32_t reaper = 0; reaper < reaperCount; ++reaper) {
        reapers.emplace_back([&, reaper] {
            for (std::int32_t index = reaper;
                index < taskCount; index += reaperCount) {
                absolute_task_destroy(tasks[static_cast<std::size_t>(index)]);
            }
        });
    }
    for (std::thread& reaper : reapers) reaper.join();
    require(completed.load(std::memory_order_relaxed) == taskCount);
}

struct ChannelRaceState {
    void* channel = nullptr;
    std::atomic<std::int64_t> sentCount{0};
    std::atomic<std::int64_t> receivedCount{0};
    std::atomic<std::int64_t> sentSum{0};
    std::atomic<std::int64_t> receivedSum{0};
};

struct ChannelRaceContext {
    ChannelRaceState* state = nullptr;
    std::int32_t worker = 0;
};

void channelRaceProducer(void* opaque) {
    auto* context = static_cast<ChannelRaceContext*>(opaque);
    constexpr std::int32_t maximumMessages = 100'000;
    const std::int64_t base =
        static_cast<std::int64_t>(context->worker + 1) * 1'000'000;
    for (std::int32_t index = 0; index < maximumMessages; ++index) {
        const std::int64_t value = base + index;
        if (!absolute_channel_send(context->state->channel, value)) return;
        context->state->sentCount.fetch_add(1, std::memory_order_relaxed);
        context->state->sentSum.fetch_add(value, std::memory_order_relaxed);
    }
}

void channelRaceConsumer(void* opaque) {
    auto* context = static_cast<ChannelRaceContext*>(opaque);
    std::int64_t value = 0;
    while (absolute_channel_receive_checked(
        context->state->channel, &value)) {
        context->state->receivedCount.fetch_add(
            1, std::memory_order_relaxed);
        context->state->receivedSum.fetch_add(
            value, std::memory_order_relaxed);
    }
}

void runChannelCloseRace() {
    constexpr std::int32_t producerCount = 4;
    constexpr std::int32_t consumerCount = 4;
    constexpr std::int32_t closerCount = 4;
    constexpr std::int64_t closeAfterMessages = 512;
    ChannelRaceState state;
    state.channel = absolute_channel_create(7);

    std::vector<void*> tasks;
    for (std::int32_t index = 0; index < consumerCount; ++index) {
        tasks.push_back(absolute_task_spawn_config(
            channelRaceConsumer,
            new ChannelRaceContext{&state, index},
            -1, -1, "close-consumer"));
    }
    for (std::int32_t index = 0; index < producerCount; ++index) {
        tasks.push_back(absolute_task_spawn_config(
            channelRaceProducer,
            new ChannelRaceContext{&state, index},
            -1, 1, "close-producer"));
    }

    std::atomic<std::int32_t> closersReady{0};
    std::vector<std::thread> closers;
    for (std::int32_t index = 0; index < closerCount; ++index) {
        closers.emplace_back([&] {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(5);
            while (state.sentCount.load(std::memory_order_relaxed) <
                    closeAfterMessages &&
                std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            closersReady.fetch_add(1, std::memory_order_acq_rel);
            while (closersReady.load(std::memory_order_acquire) <
                closerCount) {
                std::this_thread::yield();
            }
            absolute_channel_close(state.channel);
        });
    }
    for (std::thread& closer : closers) closer.join();
    for (void* task : tasks)
        delete await<ChannelRaceContext>(task);

    require(absolute_channel_is_closed(state.channel));
    require(state.sentCount.load(std::memory_order_relaxed) >=
        closeAfterMessages);
    require(state.receivedCount.load(std::memory_order_relaxed) ==
        state.sentCount.load(std::memory_order_relaxed));
    require(state.receivedSum.load(std::memory_order_relaxed) ==
        state.sentSum.load(std::memory_order_relaxed));
    absolute_channel_destroy(state.channel);
}

struct TimeoutRaceContext {
    std::int32_t delay = 0;
    std::atomic<std::int32_t>* completed = nullptr;
};

void timeoutRaceTask(void* opaque) {
    auto* context = static_cast<TimeoutRaceContext*>(opaque);
    absolute_task_delay(context->delay);
    context->completed->fetch_add(1, std::memory_order_relaxed);
}

void runTimeoutRace() {
    constexpr std::int32_t taskCount = 256;
    constexpr std::int32_t waiterCount = 8;
    std::atomic<std::int32_t> completed{0};
    std::atomic<std::int32_t> timeoutCalls{0};
    std::vector<void*> tasks;
    tasks.reserve(taskCount);
    for (std::int32_t index = 0; index < taskCount; ++index) {
        tasks.push_back(absolute_task_spawn_config(
            timeoutRaceTask,
            new TimeoutRaceContext{index % 4, &completed},
            -1, index % 7 - 3, "timeout-race"));
    }

    std::vector<std::thread> waiters;
    for (std::int32_t waiter = 0; waiter < waiterCount; ++waiter) {
        waiters.emplace_back([&, waiter] {
            for (std::int32_t index = waiter;
                index < taskCount; index += waiterCount) {
                (void)absolute_task_await_timeout(
                    tasks[static_cast<std::size_t>(index)], index % 3);
                timeoutCalls.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& waiter : waiters) waiter.join();
    for (void* task : tasks)
        delete await<TimeoutRaceContext>(task);
    require(timeoutCalls.load(std::memory_order_relaxed) == taskCount);
    require(completed.load(std::memory_order_relaxed) == taskCount);
}

struct NestedGroupContext {
    std::atomic<std::int32_t>* leaves = nullptr;
    std::atomic<std::int32_t>* cancelledLeaves = nullptr;
    std::int32_t depth = 0;
    std::int32_t fanout = 0;
};

void nestedGroupTask(void* opaque) {
    auto* context = static_cast<NestedGroupContext*>(opaque);
    if (context->depth == 0) {
        if (absolute_task_current_cancelled()) {
            context->cancelledLeaves->fetch_add(
                1, std::memory_order_relaxed);
        }
        context->leaves->fetch_add(1, std::memory_order_relaxed);
        return;
    }

    void* group = absolute_task_group_create();
    require(group != nullptr);
    for (std::int32_t index = 0; index < context->fanout; ++index) {
        auto* child = static_cast<NestedGroupContext*>(
            std::malloc(sizeof(NestedGroupContext)));
        require(child != nullptr);
        *child = {
            context->leaves,
            context->cancelledLeaves,
            context->depth - 1,
            context->fanout};
        require(absolute_task_group_add(
            group,
            absolute_task_spawn_config(
                nestedGroupTask, child, -1, index % 7 - 3, "nested-group")));
    }
    if (absolute_task_current_cancelled())
        absolute_task_group_cancel_and_join(group);
    else
        absolute_task_group_join(group);
    absolute_task_group_destroy(group);
}

void runNestedTaskGroups() {
    constexpr std::int32_t rootCount = 8;
    constexpr std::int32_t depth = 3;
    constexpr std::int32_t fanout = 3;
    std::atomic<std::int32_t> leaves{0};
    std::atomic<std::int32_t> cancelledLeaves{0};
    void* roots = absolute_task_group_create();
    require(roots != nullptr);
    for (std::int32_t index = 0; index < rootCount; ++index) {
        auto* root = static_cast<NestedGroupContext*>(
            std::malloc(sizeof(NestedGroupContext)));
        require(root != nullptr);
        *root = {&leaves, &cancelledLeaves, depth, fanout};
        require(absolute_task_group_add(
            roots,
            absolute_task_spawn_config(
                nestedGroupTask, root, -1, 0, "nested-root")));
    }
    absolute_task_group_cancel_and_join(roots);
    absolute_task_group_destroy(roots);

    std::int32_t expectedLeaves = rootCount;
    for (std::int32_t level = 0; level < depth; ++level)
        expectedLeaves *= fanout;
    require(leaves.load(std::memory_order_relaxed) == expectedLeaves);
    require(cancelledLeaves.load(std::memory_order_relaxed) ==
        expectedLeaves);
}
}  // namespace

int main() {
#if defined(_WIN32)
    _putenv_s("ABSOLUTE_SCHEDULER_WORKERS", "4");
#else
    setenv("ABSOLUTE_SCHEDULER_WORKERS", "4", 1);
#endif
    require(absolute_scheduler_worker_count() == 4);

    std::cerr << "phase=spawn-await-storm\n";
    runSpawnAwaitStorm();
    std::cerr << "phase=cancel-vs-complete\n";
    runCancelVsComplete();
    std::cerr << "phase=destroy-vs-wait\n";
    runDestroyVsWait();
    std::cerr << "phase=channel-close-race\n";
    runChannelCloseRace();
    std::cerr << "phase=timeout-race\n";
    runTimeoutRace();
    std::cerr << "phase=nested-task-groups\n";
    runNestedTaskGroups();

    std::cout << "runtime-scheduler-stress=ok\n";
    return 0;
}
