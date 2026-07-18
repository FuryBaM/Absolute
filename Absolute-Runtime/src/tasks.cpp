#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using TaskEntry = void (*)(void*);

    struct Task {
        std::mutex mutex;
        std::condition_variable completed;
        void* context = nullptr;
        bool done = false;
    };

    struct Work {
        Task* task = nullptr;
        TaskEntry entry = nullptr;
    };

    class Scheduler {
        std::mutex mutex;
        std::condition_variable available;
        std::deque<Work> queue;
        std::vector<std::thread> workers;
        bool stopping = false;

        void Run() {
            while (true) {
                Work work;
                {
                    std::unique_lock lock(mutex);
                    available.wait(lock, [&] { return stopping || !queue.empty(); });
                    if (stopping && queue.empty()) return;
                    work = queue.front();
                    queue.pop_front();
                }

                try {
                    work.entry(work.task->context);
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

        void Submit(Task* task, TaskEntry entry) {
            {
                std::lock_guard lock(mutex);
                queue.push_back({task, entry});
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

extern "C" void* absolute_task_spawn(void (*entry)(void*), void* context) {
    if (!entry || !context) {
        std::cerr << "Absolute runtime error: invalid task entry or context\n";
        std::abort();
    }
    Task* task = new Task;
    task->context = context;
    GetScheduler().Submit(task, entry);
    return task;
}

extern "C" void* absolute_task_await(void* handle) {
    if (!handle) {
        std::cerr << "Absolute runtime error: null task is awaited\n";
        std::abort();
    }
    Task* task = static_cast<Task*>(handle);
    Wait(*task);
    void* context = task->context;
    delete task;
    return context;
}

extern "C" void absolute_task_destroy(void* handle) {
    if (!handle) return;
    Task* task = static_cast<Task*>(handle);
    Wait(*task);
    std::free(task->context);
    delete task;
}
