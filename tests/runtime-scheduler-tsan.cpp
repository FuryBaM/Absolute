#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
std::int32_t absolute_task_current_priority();
bool absolute_task_current_role_is(const char* role);

void* absolute_atomic_create(std::int64_t initialValue);
std::int64_t absolute_atomic_fetch_add(void* atomic, std::int64_t value);
std::int64_t absolute_atomic_load(void* atomic);
void absolute_atomic_destroy(void* atomic);

void* absolute_mutex_create();
void absolute_mutex_lock(void* mutex);
void absolute_mutex_unlock(void* mutex);
void absolute_mutex_destroy(void* mutex);

void* absolute_channel_create(std::int32_t capacity);
bool absolute_channel_send(void* channel, std::int64_t value);
std::int64_t absolute_channel_receive(void* channel);
std::int32_t absolute_channel_count(void* channel);
void absolute_channel_close(void* channel);
void absolute_channel_destroy(void* channel);

void* absolute_cancellation_token_create();
void absolute_cancellation_token_cancel(void* token);
bool absolute_cancellation_token_is_cancelled(void* token);
void absolute_cancellation_token_destroy(void* token);
}

namespace {
struct SharedState {
    void* atomicCounter = nullptr;
    void* mutex = nullptr;
    std::int64_t guardedCounter = 0;
    std::atomic<int> metadataFailures{0};
};

struct WorkerContext {
    SharedState* shared = nullptr;
    int iterations = 0;
    int expectedPriority = 0;
    const char* expectedRole = nullptr;
};

void contentionWorker(void* opaque) {
    auto* context = static_cast<WorkerContext*>(opaque);
    if (absolute_task_current_priority() != context->expectedPriority ||
        !absolute_task_current_role_is(context->expectedRole)) {
        context->shared->metadataFailures.fetch_add(1, std::memory_order_relaxed);
    }

    for (int iteration = 0; iteration < context->iterations; ++iteration) {
        absolute_atomic_fetch_add(context->shared->atomicCounter, 1);
        absolute_mutex_lock(context->shared->mutex);
        ++context->shared->guardedCounter;
        absolute_mutex_unlock(context->shared->mutex);
    }
}

struct ChannelContext {
    void* channel = nullptr;
    int producer = -1;
    int count = 0;
    std::int64_t checksum = 0;
};

void channelProducer(void* opaque) {
    auto* context = static_cast<ChannelContext*>(opaque);
    const std::int64_t base = static_cast<std::int64_t>(context->producer + 1) * 1'000'000;
    for (int index = 0; index < context->count; ++index) {
        const std::int64_t value = base + index;
        if (!absolute_channel_send(context->channel, value)) {
            std::abort();
        }
        context->checksum += value;
    }
}

void channelConsumer(void* opaque) {
    auto* context = static_cast<ChannelContext*>(opaque);
    for (int index = 0; index < context->count; ++index) {
        context->checksum += absolute_channel_receive(context->channel);
    }
}

struct CancellationContext {
    void* token = nullptr;
    std::atomic<bool>* start = nullptr;
    int iterations = 0;
    std::int64_t observations = 0;
    bool writer = false;
};

void cancellationWorker(void* opaque) {
    auto* context = static_cast<CancellationContext*>(opaque);
    while (!context->start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (int iteration = 0; iteration < context->iterations; ++iteration) {
        if (context->writer) {
            absolute_cancellation_token_cancel(context->token);
        } else if (absolute_cancellation_token_is_cancelled(context->token)) {
            ++context->observations;
        }
    }
}

template <class Context>
Context* awaitAndDelete(void* task) {
    return static_cast<Context*>(absolute_task_await(task));
}
}  // namespace

int main() {
    constexpr int taskCount = 16;
    constexpr int iterations = 5'000;

    std::cerr << "phase=contention\n";
    SharedState shared;
    shared.atomicCounter = absolute_atomic_create(0);
    shared.mutex = absolute_mutex_create();

    std::vector<void*> workers;
    workers.reserve(taskCount);
    for (int index = 0; index < taskCount; ++index) {
        auto* context = new WorkerContext;
        context->shared = &shared;
        context->iterations = iterations;
        context->expectedPriority = index % 5 - 2;
        context->expectedRole = index % 2 == 0 ? "even" : "odd";
        workers.push_back(absolute_task_spawn_config(
            contentionWorker, context, -1, context->expectedPriority,
            context->expectedRole));
    }

    for (void* worker : workers) {
        delete awaitAndDelete<WorkerContext>(worker);
    }

    const std::int64_t expected = static_cast<std::int64_t>(taskCount) * iterations;
    assert(shared.metadataFailures.load(std::memory_order_relaxed) == 0);
    assert(absolute_atomic_load(shared.atomicCounter) == expected);
    assert(shared.guardedCounter == expected);
    absolute_atomic_destroy(shared.atomicCounter);
    absolute_mutex_destroy(shared.mutex);

    std::cerr << "phase=channel\n";
    constexpr int producerCount = 4;
    constexpr int consumerCount = 4;
    constexpr int messagesPerWorker = 5'000;
    void* channel = absolute_channel_create(17);
    std::vector<void*> producers;
    std::vector<void*> consumers;

    for (int index = 0; index < consumerCount; ++index) {
        auto* context = new ChannelContext{channel, -1, messagesPerWorker, 0};
        consumers.push_back(absolute_task_spawn_config(
            channelConsumer, context, -1, 0, "consumer"));
    }
    for (int index = 0; index < producerCount; ++index) {
        auto* context = new ChannelContext{channel, index, messagesPerWorker, 0};
        producers.push_back(absolute_task_spawn_config(
            channelProducer, context, -1, 1, "producer"));
    }

    std::int64_t produced = 0;
    std::int64_t consumed = 0;
    for (void* producer : producers) {
        auto* context = awaitAndDelete<ChannelContext>(producer);
        produced += context->checksum;
        delete context;
    }
    for (void* consumer : consumers) {
        auto* context = awaitAndDelete<ChannelContext>(consumer);
        consumed += context->checksum;
        delete context;
    }

    assert(produced == consumed);
    assert(absolute_channel_count(channel) == 0);
    absolute_channel_close(channel);
    absolute_channel_destroy(channel);

    std::cerr << "phase=cancellation\n";
    void* token = absolute_cancellation_token_create();
    std::atomic<bool> startCancellationRace{false};
    std::vector<void*> cancellationTasks;
    for (int index = 0; index < 2; ++index) {
        auto* context = new CancellationContext{
            token, &startCancellationRace, 2'000, 0, true};
        cancellationTasks.push_back(absolute_task_spawn_config(
            cancellationWorker, context, -1, 2, "cancel-writer"));
    }
    for (int index = 0; index < 4; ++index) {
        auto* context = new CancellationContext{
            token, &startCancellationRace, 20'000, 0, false};
        cancellationTasks.push_back(absolute_task_spawn_config(
            cancellationWorker, context, -1, 0, "cancel-reader"));
    }
    startCancellationRace.store(true, std::memory_order_release);

    std::int64_t cancellationObservations = 0;
    for (void* task : cancellationTasks) {
        auto* context = awaitAndDelete<CancellationContext>(task);
        cancellationObservations += context->observations;
        delete context;
    }
    assert(absolute_cancellation_token_is_cancelled(token));
    assert(cancellationObservations > 0);
    absolute_cancellation_token_destroy(token);

    std::cout << "runtime-scheduler-tsan=ok\n";
    return 0;
}
