#include "socket_reactor.h"

#include <cerrno>

#include "scheduler_io.h"

#if defined(__linux__)

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Absolute::RuntimeDetail {
    namespace {
        class EpollReactor;

        struct SocketWait {
            EpollReactor* reactor = nullptr;
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
        };

        class EpollReactor {
            int pollDescriptor = -1;
            int controlDescriptor = -1;
            int initializationError = 0;
            std::mutex mutex;
            std::deque<SocketWait*> pending;
            std::unordered_map<int, DescriptorWaits> active;
            std::thread worker;
            std::atomic<bool> stopping{false};

            void Wake() {
                if (controlDescriptor < 0) return;
                const std::uint64_t value = 1;
                const ssize_t ignored =
                    write(controlDescriptor, &value, sizeof(value));
                (void)ignored;
            }

            void DrainControl() {
                std::uint64_t value = 0;
                while (read(controlDescriptor, &value, sizeof(value)) > 0) {
                }
            }

            static std::uint32_t NativeEvents(std::uint32_t requested) {
                std::uint32_t result = EPOLLONESHOT | EPOLLERR | EPOLLHUP;
                if ((requested & SocketReadyRead) != 0) result |= EPOLLIN;
                if ((requested & SocketReadyWrite) != 0) result |= EPOLLOUT;
                return result;
            }

            static std::uint32_t PortableEvents(std::uint32_t nativeEvents) {
                std::uint32_t result = 0;
                if ((nativeEvents & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0)
                    result |= SocketReadyRead;
                if ((nativeEvents & (EPOLLOUT | EPOLLHUP | EPOLLERR)) != 0)
                    result |= SocketReadyWrite;
                return result;
            }

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

            bool Arm(
                int descriptor, const DescriptorWaits& waits, int operation) {
                epoll_event event{};
                event.events = NativeEvents(RequestedEvents(waits));
                event.data.fd = descriptor;
                return epoll_ctl(
                    pollDescriptor, operation, descriptor, &event) == 0;
            }

            void FailDescriptor(
                std::unordered_map<int, DescriptorWaits>::iterator descriptor,
                int error) {
                std::vector<SocketWait*> failed =
                    std::move(descriptor->second.waits);
                const int nativeDescriptor = descriptor->first;
                active.erase(descriptor);
                epoll_ctl(
                    pollDescriptor, EPOLL_CTL_DEL,
                    nativeDescriptor, nullptr);
                for (SocketWait* wait : failed) {
                    wait->error = error;
                    Complete(wait);
                }
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
                    descriptor->second.waits.push_back(wait);
                    if (!Arm(
                            wait->descriptor, descriptor->second,
                            inserted ? EPOLL_CTL_ADD : EPOLL_CTL_MOD)) {
                        const int error = errno;
                        FailDescriptor(descriptor, error);
                    }
                }
            }

            void ProcessReady(int descriptor, std::uint32_t nativeEvents) {
                auto found = active.find(descriptor);
                if (found == active.end()) return;

                const std::uint32_t ready = PortableEvents(nativeEvents);
                const bool terminal =
                    (nativeEvents & (EPOLLERR | EPOLLHUP)) != 0;
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
                    active.erase(found);
                    epoll_ctl(
                        pollDescriptor, EPOLL_CTL_DEL, descriptor, nullptr);
                }
                else {
                    found->second.waits = std::move(retained);
                    if (!Arm(descriptor, found->second, EPOLL_CTL_MOD)) {
                        const int error = errno;
                        FailDescriptor(found, error);
                    }
                }

                for (SocketWait* wait : completed) {
                    wait->ready = ready;
                    Complete(wait);
                }
            }

            int PollTimeoutMilliseconds() const {
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
                if (!foundDeadline) return -1;

                const auto remaining =
                    nearest - std::chrono::steady_clock::now();
                if (remaining <= std::chrono::steady_clock::duration::zero())
                    return 0;
                const auto rounded =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        remaining + std::chrono::milliseconds(1) -
                        std::chrono::steady_clock::duration{1});
                return rounded.count() > INT_MAX
                    ? INT_MAX : static_cast<int>(rounded.count());
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
                        epoll_ctl(
                            pollDescriptor, EPOLL_CTL_DEL,
                            nativeDescriptor, nullptr);
                        descriptor = active.erase(descriptor);
                        continue;
                    }

                    descriptor->second.waits = std::move(retained);
                    if (!Arm(
                            nativeDescriptor, descriptor->second,
                            EPOLL_CTL_MOD)) {
                        const int error = errno;
                        for (SocketWait* wait :
                             descriptor->second.waits) {
                            wait->error = error;
                            completed.push_back(wait);
                        }
                        epoll_ctl(
                            pollDescriptor, EPOLL_CTL_DEL,
                            nativeDescriptor, nullptr);
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
                    (void)descriptor;
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
                epoll_event events[64]{};
                while (!stopping.load(std::memory_order_acquire)) {
                    ProcessExpired();
                    const int count = epoll_wait(
                        pollDescriptor, events,
                        static_cast<int>(
                            sizeof(events) / sizeof(events[0])),
                        PollTimeoutMilliseconds());
                    if (count < 0) {
                        if (errno == EINTR) continue;
                        const int error = errno;
                        stopping.store(true, std::memory_order_release);
                        CancelActive(error);
                        ProcessPending();
                        return;
                    }
                    for (int index = 0; index < count; ++index) {
                        if (events[index].data.fd == controlDescriptor) {
                            DrainControl();
                            ProcessPending();
                            continue;
                        }
                        ProcessReady(
                            events[index].data.fd, events[index].events);
                    }
                    ProcessExpired();
                }
                ProcessPending();
                CancelActive(ECANCELED);
            }

        public:
            EpollReactor() {
                pollDescriptor = epoll_create1(EPOLL_CLOEXEC);
                if (pollDescriptor < 0) {
                    initializationError = errno;
                    return;
                }
                controlDescriptor = eventfd(
                    0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (controlDescriptor < 0) {
                    initializationError = errno;
                    close(pollDescriptor);
                    pollDescriptor = -1;
                    return;
                }
                epoll_event event{};
                event.events = EPOLLIN;
                event.data.fd = controlDescriptor;
                if (epoll_ctl(
                        pollDescriptor, EPOLL_CTL_ADD,
                        controlDescriptor, &event) != 0) {
                    initializationError = errno;
                    close(controlDescriptor);
                    close(pollDescriptor);
                    controlDescriptor = -1;
                    pollDescriptor = -1;
                    return;
                }
                worker = std::thread([this] { Run(); });
            }

            ~EpollReactor() {
                stopping.store(true, std::memory_order_release);
                Wake();
                if (worker.joinable()) worker.join();
                if (controlDescriptor >= 0) close(controlDescriptor);
                if (pollDescriptor >= 0) close(pollDescriptor);
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

        EpollReactor& GetEpollReactor() {
            static EpollReactor reactor;
            return reactor;
        }

        void RegisterSocketWait(
            void* opaque, IoCompletion completion, void* completionContext) {
            auto* wait = static_cast<SocketWait*>(opaque);
            wait->completion = completion;
            wait->completionContext = completionContext;
            wait->reactor->Submit(wait);
        }
    }

    bool WaitSocketReady(
        int descriptor, std::uint32_t events,
        std::int32_t timeoutMilliseconds) {
        EpollReactor& reactor = GetEpollReactor();
        SocketWait wait{&reactor, descriptor, events};
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

#elif defined(_WIN32)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Absolute::RuntimeDetail {
    namespace {
        constexpr ULONG_PTR StopCompletionKey = 1;
        constexpr ULONG_PTR DeadlineChangedCompletionKey = 2;

        struct IocpWait {
            OVERLAPPED overlapped{};
            SOCKET socket = INVALID_SOCKET;
            SocketOperationStart start = nullptr;
            void* startContext = nullptr;
            SocketCompletionResult* result = nullptr;
            std::chrono::steady_clock::time_point deadline{};
            bool hasDeadline = false;
            bool timeoutRequested = false;
            bool operationStarted = false;
            IoCompletion completion = nullptr;
            void* completionContext = nullptr;
        };

        static_assert(offsetof(IocpWait, overlapped) == 0);

        class IocpReactor {
            HANDLE port = nullptr;
            int initializationError = 0;
            std::mutex mutex;
            std::unordered_set<IocpWait*> active;
            std::thread worker;
            std::atomic<bool> stopping{false};

            static void Complete(IocpWait* wait) {
                IoCompletion completion = wait->completion;
                void* context = wait->completionContext;
                completion(context);
            }

            DWORD CompletionTimeout() {
                std::lock_guard lock(mutex);
                bool foundDeadline = false;
                std::chrono::steady_clock::time_point nearest{};
                for (const IocpWait* wait : active) {
                    if (!wait->operationStarted ||
                        !wait->hasDeadline ||
                        wait->timeoutRequested)
                        continue;
                    if (!foundDeadline || wait->deadline < nearest) {
                        nearest = wait->deadline;
                        foundDeadline = true;
                    }
                }
                if (!foundDeadline) return INFINITE;

                const auto remaining =
                    nearest - std::chrono::steady_clock::now();
                if (remaining <= std::chrono::steady_clock::duration::zero())
                    return 0;
                const auto rounded =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        remaining + std::chrono::milliseconds(1) -
                        std::chrono::steady_clock::duration{1});
                return rounded.count() >=
                        static_cast<long long>(INFINITE)
                    ? INFINITE - 1
                    : static_cast<DWORD>(rounded.count());
            }

            void CancelExpired() {
                const auto now = std::chrono::steady_clock::now();
                // Keep the registry lock through CancelIoEx. The completion
                // thread removes a wait under the same lock before resuming
                // its fiber, so stack-backed OVERLAPPED storage cannot
                // disappear while cancellation still references it.
                std::lock_guard lock(mutex);
                for (IocpWait* wait : active) {
                    if (!wait->hasDeadline ||
                        !wait->operationStarted ||
                        wait->timeoutRequested ||
                        wait->deadline > now) {
                        continue;
                    }
                    wait->timeoutRequested = true;
                    CancelIoEx(
                        reinterpret_cast<HANDLE>(wait->socket),
                        &wait->overlapped);
                }
            }

            void Run() {
                while (!stopping.load(std::memory_order_acquire)) {
                    DWORD bytesTransferred = 0;
                    ULONG_PTR completionKey = 0;
                    OVERLAPPED* overlapped = nullptr;
                    const BOOL succeeded = GetQueuedCompletionStatus(
                        port, &bytesTransferred, &completionKey,
                        &overlapped, CompletionTimeout());
                    const DWORD completionError =
                        succeeded ? ERROR_SUCCESS : GetLastError();
                    if (!overlapped) {
                        if (completionKey == StopCompletionKey) return;
                        if (completionKey ==
                                DeadlineChangedCompletionKey ||
                            (!succeeded &&
                                completionError == WAIT_TIMEOUT)) {
                            CancelExpired();
                        }
                        continue;
                    }

                    auto* wait = reinterpret_cast<IocpWait*>(overlapped);
                    {
                        std::lock_guard lock(mutex);
                        active.erase(wait);
                    }
                    wait->result->bytesTransferred =
                        static_cast<std::uint32_t>(bytesTransferred);
                    wait->result->error = wait->timeoutRequested
                        ? WSAETIMEDOUT
                        : (succeeded
                            ? 0 : static_cast<int>(completionError));
                    Complete(wait);
                }
            }

        public:
            IocpReactor() {
                port = CreateIoCompletionPort(
                    INVALID_HANDLE_VALUE, nullptr, 0, 1);
                if (!port) {
                    initializationError = static_cast<int>(GetLastError());
                    return;
                }
                worker = std::thread([this] { Run(); });
            }

            ~IocpReactor() {
                stopping.store(true, std::memory_order_release);
                if (port)
                    PostQueuedCompletionStatus(
                        port, 0, StopCompletionKey, nullptr);
                if (worker.joinable()) worker.join();
                if (port) CloseHandle(port);
            }

            bool Associate(SOCKET socket, int& error) {
                if (initializationError != 0) {
                    error = initializationError;
                    return false;
                }
                if (stopping.load(std::memory_order_acquire)) {
                    error = ERROR_OPERATION_ABORTED;
                    return false;
                }
                HANDLE associated = CreateIoCompletionPort(
                    reinterpret_cast<HANDLE>(socket), port, 0, 0);
                if (!associated) {
                    error = static_cast<int>(GetLastError());
                    return false;
                }
                error = 0;
                return true;
            }

            void Submit(IocpWait* wait) {
                const int unavailable =
                    initializationError != 0
                        ? initializationError
                        : (stopping.load(std::memory_order_acquire)
                            ? ERROR_OPERATION_ABORTED : 0);
                if (unavailable != 0) {
                    wait->result->error = unavailable;
                    Complete(wait);
                    return;
                }

                {
                    std::lock_guard lock(mutex);
                    active.insert(wait);
                }
                const int started =
                    wait->start(wait->startContext, &wait->overlapped);
                if (started == SOCKET_ERROR) {
                    const int error = WSAGetLastError();
                    if (error != WSA_IO_PENDING) {
                        {
                            std::lock_guard lock(mutex);
                            active.erase(wait);
                        }
                        wait->result->error = error;
                        Complete(wait);
                        return;
                    }
                }
                bool registered = false;
                {
                    std::lock_guard lock(mutex);
                    const auto found = active.find(wait);
                    if (found != active.end()) {
                        wait->operationStarted = true;
                        registered = true;
                    }
                }
                if (registered && wait->hasDeadline) {
                    PostQueuedCompletionStatus(
                        port, 0, DeadlineChangedCompletionKey, nullptr);
                }
            }
        };

        IocpReactor& GetIocpReactor() {
            static IocpReactor reactor;
            return reactor;
        }

        void RegisterSocketCompletion(
            void* opaque, IoCompletion completion, void* completionContext) {
            auto* wait = static_cast<IocpWait*>(opaque);
            wait->completion = completion;
            wait->completionContext = completionContext;
            GetIocpReactor().Submit(wait);
        }
    }

    bool WaitSocketReady(int, std::uint32_t, std::int32_t) {
        errno = ENOTSUP;
        return false;
    }

    bool RunSocketCompletion(
        SOCKET socket, SocketOperationStart start, void* context,
        SocketCompletionResult& result,
        std::int32_t timeoutMilliseconds) {
        if (socket == INVALID_SOCKET || !start) {
            result.error = WSAEINVAL;
            return false;
        }
        IocpWait wait{};
        wait.socket = socket;
        wait.start = start;
        wait.startContext = context;
        wait.result = &result;
        if (timeoutMilliseconds > 0) {
            wait.deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(timeoutMilliseconds);
            wait.hasDeadline = true;
        }
        SuspendForIo(&RegisterSocketCompletion, &wait);
        return result.error == 0;
    }

    bool AssociateSocketCompletion(SOCKET socket, int& error) {
        if (socket == INVALID_SOCKET) {
            error = WSAEINVAL;
            return false;
        }
        return GetIocpReactor().Associate(socket, error);
    }
}

#elif !defined(__APPLE__)

namespace Absolute::RuntimeDetail {
    bool WaitSocketReady(int, std::uint32_t, std::int32_t) {
        errno = ENOTSUP;
        return false;
    }
}

#endif
