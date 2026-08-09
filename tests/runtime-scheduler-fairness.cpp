#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
void absolute_task_delay(std::int32_t milliseconds);
std::int32_t absolute_scheduler_worker_count();
std::int32_t absolute_task_current_priority();
}

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}

struct Distribution {
    std::atomic<std::int32_t> completed{0};
    std::vector<std::int32_t> order;
};

struct RecordContext {
    Distribution* distribution = nullptr;
    std::int32_t value = 0;
    std::int32_t expectedPriority = 0;
};

void recordValue(void* opaque) {
    auto* context = static_cast<RecordContext*>(opaque);
    require(absolute_task_current_priority() ==
        context->expectedPriority);
    context->distribution->order.push_back(context->value);
    context->distribution->completed.fetch_add(
        1, std::memory_order_release);
}

struct BarrierContext {
    Distribution* distribution = nullptr;
    std::int32_t expected = 0;
};

void completionBarrier(void* opaque) {
    auto* context = static_cast<BarrierContext*>(opaque);
    while (context->distribution->completed.load(
        std::memory_order_acquire) < context->expected) {
        absolute_task_delay(1);
    }
}

struct StarvationContext {
    std::atomic<std::int32_t> highSteps{0};
    std::int32_t observedHighSteps = -1;
};

struct HighChainContext {
    StarvationContext* starvation = nullptr;
    std::int32_t remaining = 0;
};

void highPriorityChain(void* opaque) {
    auto* context = static_cast<HighChainContext*>(opaque);
    context->starvation->highSteps.fetch_add(
        1, std::memory_order_release);
    if (context->remaining > 1) {
        void* child = absolute_task_spawn_config(
            highPriorityChain,
            new HighChainContext{
                context->starvation, context->remaining - 1},
            -1, 3, "continuous-high");
        delete static_cast<HighChainContext*>(
            absolute_task_await(child));
    }
}

void lowPriorityProbe(void* opaque) {
    auto* context = static_cast<StarvationContext*>(opaque);
    context->observedHighSteps =
        context->highSteps.load(std::memory_order_acquire);
}

template <class T>
T* await(void* task) {
    return static_cast<T*>(absolute_task_await(task));
}

struct FairnessResult {
    bool weightedPriority = false;
    bool roleRoundRobin = false;
    bool starvationProtected = false;
};

void fairnessParent(void* opaque) {
    auto* result = static_cast<FairnessResult*>(opaque);

    constexpr std::int32_t tasksPerPriority = 256;
    constexpr std::int32_t priorityCount = 7;
    constexpr std::int32_t totalPriorityTasks =
        tasksPerPriority * priorityCount;
    Distribution priorities;
    priorities.order.reserve(totalPriorityTasks);
    std::vector<void*> priorityTasks;
    priorityTasks.reserve(totalPriorityTasks);
    for (std::int32_t priority = -3; priority <= 3; ++priority) {
        for (std::int32_t index = 0;
            index < tasksPerPriority; ++index) {
            priorityTasks.push_back(absolute_task_spawn_config(
                recordValue,
                new RecordContext{&priorities, priority, priority},
                -1, priority, "weighted-priority"));
        }
    }
    void* priorityBarrier = absolute_task_spawn_config(
        completionBarrier,
        new BarrierContext{&priorities, totalPriorityTasks},
        -1, -3, "priority-barrier");
    delete await<BarrierContext>(priorityBarrier);
    for (void* task : priorityTasks)
        delete await<RecordContext>(task);

    constexpr size_t sampleSize = 360;
    require(priorities.order.size() >= sampleSize);
    std::array<std::int32_t, priorityCount> prioritySamples{};
    size_t firstLowest = priorities.order.size();
    for (size_t index = 0; index < sampleSize; ++index) {
        const std::int32_t priority = priorities.order[index];
        ++prioritySamples[static_cast<size_t>(priority + 3)];
        if (priority == -3 && firstLowest == priorities.order.size())
            firstLowest = index;
    }
    bool increasing = prioritySamples[0] > 0;
    for (size_t index = 1; index < prioritySamples.size(); ++index)
        increasing =
            increasing && prioritySamples[index] > prioritySamples[index - 1];
    result->weightedPriority = increasing && firstLowest < 128;

    constexpr std::int32_t roleCount = 3;
    constexpr std::int32_t tasksPerRole = 120;
    constexpr std::int32_t totalRoleTasks = roleCount * tasksPerRole;
    Distribution roles;
    roles.order.reserve(totalRoleTasks);
    std::vector<void*> roleTasks;
    roleTasks.reserve(totalRoleTasks);
    constexpr std::array<const char*, roleCount> roleNames{
        "alpha", "beta", "gamma"};
    for (std::int32_t role = 0; role < roleCount; ++role) {
        for (std::int32_t index = 0; index < tasksPerRole; ++index) {
            roleTasks.push_back(absolute_task_spawn_config(
                recordValue,
                new RecordContext{&roles, role, 0},
                -1, 0, roleNames[static_cast<size_t>(role)]));
        }
    }
    void* roleBarrier = absolute_task_spawn_config(
        completionBarrier,
        new BarrierContext{&roles, totalRoleTasks},
        -1, -3, "role-barrier");
    delete await<BarrierContext>(roleBarrier);
    for (void* task : roleTasks)
        delete await<RecordContext>(task);

    std::array<std::int32_t, roleCount> roleSamples{};
    for (size_t index = 0; index < 90; ++index)
        ++roleSamples[static_cast<size_t>(roles.order[index])];
    const auto [minimumRole, maximumRole] =
        std::minmax_element(roleSamples.begin(), roleSamples.end());
    result->roleRoundRobin = *maximumRole - *minimumRole <= 1;

    auto* starvation = new StarvationContext;
    void* high = absolute_task_spawn_config(
        highPriorityChain,
        new HighChainContext{starvation, 96},
        -1, 3, "continuous-high");
    void* low = absolute_task_spawn_config(
        lowPriorityProbe, starvation,
        -1, -3, "low-probe");
    starvation = await<StarvationContext>(low);
    result->starvationProtected =
        starvation->observedHighSteps > 0 &&
        starvation->observedHighSteps < 64;
    auto* highContext = await<HighChainContext>(high);
    require(highContext->starvation == starvation);
    delete highContext;
    delete starvation;
}
}  // namespace

int main() {
#if defined(_WIN32)
    _putenv_s("ABSOLUTE_SCHEDULER_WORKERS", "1");
#else
    setenv("ABSOLUTE_SCHEDULER_WORKERS", "1", 1);
#endif

    require(absolute_scheduler_worker_count() == 1);
    auto* result = new FairnessResult;
    void* parent = absolute_task_spawn_config(
        fairnessParent, result, -1, 0, "fairness-parent");
    result = await<FairnessResult>(parent);
    require(result->weightedPriority);
    require(result->roleRoundRobin);
    require(result->starvationProtected);
    delete result;

    std::cout << "runtime-scheduler-fairness=ok\n";
    return 0;
}
