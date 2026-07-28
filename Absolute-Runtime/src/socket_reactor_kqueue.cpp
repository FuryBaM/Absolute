#include "socket_reactor.h"

#if defined(__APPLE__)

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include "scheduler_io.h"

namespace Absolute::RuntimeDetail {
    namespace {
        constexpr std::uintptr_t ControlIdentifier = 1;

        struct SocketWait {
            int descriptor = -1;
            std::uint32_t requested = 0;
            std::uint32_t ready = 0;
            int error = 0;
            std::chrono::steady_clock::time_point deadline{};
            bool hasDeadline = false;
            IoCompletion completion = nullptr;
            void* completionContext = nullptr;
        };

        struct DescriptorWaits {
            std::vector<SocketWait*> waits;
            std::uint32_t armed = 0;
        };

        class KqueueReactor {
            int queueDescriptor = -1;
            int initializationError = 0;
            std::mutex mutex;
            std::deque<SocketWait*> pending;
            std::unordered_map<int, DescriptorWaits> active;
            std::thread worker;
            std::atomic<bool> stopping{false};

            static void Complete(SocketWait* wait) {
                IoCompletion completion = wait->completion;
                void* context = wait->completionContext;
                completion(context);
            }

            static std::uint32_t RequestedEvents(
                const DescriptorWaits& descriptor) {
                std::uint32_t requested = 0;
                for (const SocketWait* wait : descriptor.waits)
                    requested |= wait->requested;
                return requested;
            }

            static std::int16_t NativeFilter(std::uint32_t event) {
                return event == SocketReadyRead
                    ? EVFILT_READ : EVFILT_WRITE;
            }

            bool ChangeFilter(
                int descriptor, std::uint32_t event,
                std::uint16_t flags) {
                struct kevent change{};
                EV_SET(
                    &change, static_cast<std::uintptr_t>(descriptor),
                    NativeFilter(event), flags, 0, 0, nullptr);
                return kevent(
                    queueDescriptor, &change, 1,
                    nullptr, 0, nullptr) == 0;
            }

            bool DeleteFilter(
                int descriptor, DescriptorWaits& waits,
                std::uint32_t event) {
                if ((waits.armed & event) == 0) return true;
                if (!ChangeFilter(descriptor, event, EV_DELETE) &&
                    errno != ENOENT) {
                    return false;
                }
                waits.armed &= ~event;
                return true;
            }

            bool Arm(int descriptor, DescriptorWaits& waits) {
                const std::uint32_t requested = RequestedEvents(waits);
                constexpr std::uint32_t events[] = {
                    SocketReadyRead, SocketReadyWrite};
                for (const std::uint32_t event : events) {
                    if ((requested & event) != 0) {
                        if (!ChangeFilter(
                                descriptor, event,
                                EV_ADD | EV_ONESHOT)) {
                            return false;
                        }
                        waits.armed |= event;
                    }
                    else {
                        if (!DeleteFilter(descriptor, waits, event))
                            return false;
                    }
                }
                return true;
            }

            void DeleteFilters(
                int descriptor, DescriptorWaits& waits) {
                DeleteFilter(descriptor, waits, SocketReadyRead);
                DeleteFilter(descriptor, waits, SocketReadyWrite);
            }

            void FailDescriptor(
                std::unordered_map<int, DescriptorWaits>::iterator descriptor,
                int error) {
                std::vector<SocketWait*> failed =
                    std::move(descriptor->second.waits);
                const int nativeDescriptor = descriptor->first;
                DeleteFilters(nativeDescriptor, descriptor->second);
                active.erase(descriptor);
                for (SocketWait* wait : failed) {
                    wait->error = error;
                    Complete(wait);
                }
            }

            void Wake() {
                if (queueDescriptor < 0) return;
                struct kevent trigger{};
                EV_SET(
                    &trigger, ControlIdentifier, EVFILT_USER,
                    0, NOTE_TRIGGER, 0, nullptr);
                const int ignored = kevent(
                    queueDescriptor, &trigger, 1,
                    nullptr, 0, nullptr);
                (void)ignored;
            }

            void ProcessPending() {
                std::deque<SocketWait*> registrations;
                {
                    std::lock_guard lock(mutex);
                    registrations.swap(pending);
                }
                for (SocketWait* wait : registrations) {
                    if (stopping.load(std::memory_order_acquire)) {
                        wait->error = ECANCELED;
                        Complete(wait);
                        continue;
                    }

                    auto [descriptor, inserted] =
                        active.try_emplace(wait->descriptor);
                    (void)inserted;
                    descriptor->second.waits.push_back(wait);
                    if (!Arm(wait->descriptor, descriptor->second)) {
                        const int error = errno;
                        FailDescriptor(descriptor, error);
                    }
                }
            }

            void ProcessReady(const struct kevent& event) {
                const int descriptor =
                    static_cast<int>(event.ident);
                auto found = active.find(descriptor);
                if (found == active.end()) return;

                if ((event.flags & EV_ERROR) != 0) {
                    const int error = event.data != 0
                        ? static_cast<int>(event.data) : EIO;
                    FailDescriptor(found, error);
                    return;
                }

                const std::uint32_t nativeReady =
                    event.filter == EVFILT_READ
                        ? SocketReadyRead : SocketReadyWrite;
                const bool terminal = (event.flags & EV_EOF) != 0;
                const std::uint32_t ready = terminal
                    ? SocketReadyRead | SocketReadyWrite
                    : nativeReady;
                found->second.armed &= ~nativeReady;
                std::vector<SocketWait*> completed;
                std::vector<SocketWait*> retained;
                completed.reserve(found->second.waits.size());
                retained.reserve(found->second.waits.size());
                for (SocketWait* wait : found->second.waits) {
                    if (terminal || (wait->requested & ready) != 0)
                        completed.push_back(wait);
                    else
                        retained.push_back(wait);
                }

                if (retained.empty()) {
                    DeleteFilters(descriptor, found->second);
                    active.erase(found);
                }
                else {
                    found->second.waits = std::move(retained);
                    if (!Arm(descriptor, found->second)) {
                        const int error = errno;
                        FailDescriptor(found, error);
                    }
                }

                for (SocketWait* wait : completed) {
                    wait->ready = ready;
                    Complete(wait);
                }
            }

            bool NextTimeout(timespec& timeout) const {
                bool foundDeadline = false;
                std::chrono::steady_clock::time_point nearest{};
                for (const auto& [descriptor, waits] : active) {
                    (void)descriptor;
                    for (const SocketWait* wait : waits.waits) {
                        if (!wait->hasDeadline) continue;
                        if (!foundDeadline || wait->deadline < nearest) {
                            nearest = wait->deadline;
                            foundDeadline = true;
                        }
                    }
                }
                if (!foundDeadline) return false;

                auto remaining =
                    nearest - std::chrono::steady_clock::now();
                if (remaining < std::chrono::steady_clock::duration::zero())
                    remaining = std::chrono::steady_clock::duration::zero();
                const auto nanoseconds =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        remaining);
                timeout.tv_sec = static_cast<time_t>(
                    nanoseconds.count() / 1'000'000'000);
                timeout.tv_nsec = static_cast<long>(
                    nanoseconds.count() % 1'000'000'000);
                return true;
            }

            void ProcessExpired() {
                const auto now = std::chrono::steady_clock::now();
                std::vector<SocketWait*> completed;
                for (auto descriptor = active.begin();
                     descriptor != active.end();) {
                    std::vector<SocketWait*> retained;
                    retained.reserve(descriptor->second.waits.size());
                    for (SocketWait* wait : descriptor->second.waits) {
                        if (wait->hasDeadline && wait->deadline <= now) {
                            wait->error = ETIMEDOUT;
                            completed.push_back(wait);
                        }
                        else {
                            retained.push_back(wait);
                        }
                    }
                    if (retained.size() ==
                        descriptor->second.waits.size()) {
                        ++descriptor;
                        continue;
                    }

                    const int nativeDescriptor = descriptor->first;
                    if (retained.empty()) {
                        DeleteFilters(
                            nativeDescriptor, descriptor->second);
                        descriptor = active.erase(descriptor);
                        continue;
                    }

                    descriptor->second.waits = std::move(retained);
                    if (!Arm(nativeDescriptor, descriptor->second)) {
                        const int error = errno;
                        for (SocketWait* wait :
                             descriptor->second.waits) {
                            wait->error = error;
                            completed.push_back(wait);
                        }
                        DeleteFilters(
                            nativeDescriptor, descriptor->second);
                        descriptor = active.erase(descriptor);
                        continue;
                    }
                    ++descriptor;
                }
                for (SocketWait* wait : completed) Complete(wait);
            }

            void CancelActive(int error) {
                std::vector<SocketWait*> cancelled;
                for (auto& [descriptor, waits] : active) {
                    DeleteFilters(descriptor, waits);
                    cancelled.insert(
                        cancelled.end(),
                        waits.waits.begin(), waits.waits.end());
                }
                active.clear();
                for (SocketWait* wait : cancelled) {
                    wait->error = error;
                    Complete(wait);
                }
            }

            void Run() {
                struct kevent events[64]{};
                while (!stopping.load(std::memory_order_acquire)) {
                    ProcessExpired();
                    timespec timeout{};
                    const timespec* timeoutPointer =
                        NextTimeout(timeout) ? &timeout : nullptr;
                    const int count = kevent(
                        queueDescriptor, nullptr, 0,
                        events,
                        static_cast<int>(
                            sizeof(events) / sizeof(events[0])),
                        timeoutPointer);
                    if (count < 0) {
                        if (errno == EINTR) continue;
                        const int error = errno;
                        stopping.store(true, std::memory_order_release);
                        CancelActive(error);
                        ProcessPending();
                        return;
                    }
                    for (int index = 0; index < count; ++index) {
                        if (events[index].filter == EVFILT_USER &&
                            events[index].ident == ControlIdentifier) {
                            ProcessPending();
                            continue;
                        }
                        ProcessReady(events[index]);
                    }
                    ProcessExpired();
                }
                ProcessPending();
                CancelActive(ECANCELED);
            }

        public:
            KqueueReactor() {
                queueDescriptor = kqueue();
                if (queueDescriptor < 0) {
                    initializationError = errno;
                    return;
                }
                struct kevent control{};
                EV_SET(
                    &control, ControlIdentifier, EVFILT_USER,
                    EV_ADD | EV_CLEAR, 0, 0, nullptr);
                if (kevent(
                        queueDescriptor, &control, 1,
                        nullptr, 0, nullptr) != 0) {
                    initializationError = errno;
                    close(queueDescriptor);
                    queueDescriptor = -1;
                    return;
                }
                worker = std::thread([this] { Run(); });
            }

            ~KqueueReactor() {
                stopping.store(true, std::memory_order_release);
                Wake();
                if (worker.joinable()) worker.join();
                if (queueDescriptor >= 0) close(queueDescriptor);
            }

            void Submit(SocketWait* wait) {
                const int unavailable =
                    initializationError != 0
                        ? initializationError
                        : (stopping.load(std::memory_order_acquire)
                            ? ECANCELED : 0);
                if (unavailable != 0) {
                    wait->error = unavailable;
                    Complete(wait);
                    return;
                }
                {
                    std::lock_guard lock(mutex);
                    if (stopping.load(std::memory_order_acquire)) {
                        wait->error = ECANCELED;
                        Complete(wait);
                        return;
                    }
                    pending.push_back(wait);
                }
                Wake();
            }
        };

        KqueueReactor& GetKqueueReactor() {
            static KqueueReactor reactor;
            return reactor;
        }

        void RegisterSocketWait(
            void* opaque, IoCompletion completion,
            void* completionContext) {
            auto* wait = static_cast<SocketWait*>(opaque);
            wait->completion = completion;
            wait->completionContext = completionContext;
            GetKqueueReactor().Submit(wait);
        }
    }

    bool WaitSocketReady(
        int descriptor, std::uint32_t events,
        std::int32_t timeoutMilliseconds) {
        SocketWait wait{descriptor, events};
        if (timeoutMilliseconds > 0) {
            wait.deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(timeoutMilliseconds);
            wait.hasDeadline = true;
        }
        SuspendForIo(&RegisterSocketWait, &wait);
        if (wait.error != 0) {
            errno = wait.error;
            return false;
        }
        return (wait.ready & events) != 0;
    }
}

#endif
