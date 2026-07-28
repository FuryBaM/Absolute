#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "scheduler_io.h"
#include "socket_reactor.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <mswsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {
#if defined(_WIN32)
    using NativeSocket = SOCKET;
    constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket InvalidSocket = -1;
#endif

    struct SocketState {
        NativeSocket socket = InvalidSocket;
        std::int32_t timeoutMilliseconds = 0;
    };

    thread_local std::string lastNetworkError;

    template <class Result, class Operation>
    Result RunNetworkIo(Operation&& operation) {
        Result result{};
        std::string error;
        Absolute::RuntimeDetail::RunBlockingIo([&] {
            result = operation();
            error = lastNetworkError;
        });
        lastNetworkError = std::move(error);
        return result;
    }

    template <class Result, class Operation>
    Result RunSocketIo(SocketState* state, Operation&& operation) {
#if defined(__linux__) || defined(__APPLE__)
        // epoll/kqueue own both readiness and optional deadlines.
        (void)state;
        return operation();
#elif defined(_WIN32)
        // Scheduler socket operations use overlapped Winsock + IOCP. Timed
        // operations are cancelled by the IOCP deadline queue.
        if (state && Absolute::RuntimeDetail::IsSchedulerTask()) {
            return operation();
        }
        return RunNetworkIo<Result>(std::forward<Operation>(operation));
#else
        (void)state;
        return RunNetworkIo<Result>(std::forward<Operation>(operation));
#endif
    }

    void CloseNative(NativeSocket socket) {
        if (socket == InvalidSocket) return;
#if defined(_WIN32)
        closesocket(socket);
#else
        close(socket);
#endif
    }

    void CaptureSocketError(const char* operation) {
#if defined(_WIN32)
        lastNetworkError = std::string(operation) + " failed with Winsock error " +
            std::to_string(WSAGetLastError());
#else
        lastNetworkError = std::string(operation) + ": " + std::strerror(errno);
#endif
    }

#if defined(_WIN32)
    void CaptureCompletionError(const char* operation, int error) {
        if (error == WSAETIMEDOUT) {
            lastNetworkError =
                std::string(operation) + ": operation timed out";
            return;
        }
        WSASetLastError(error);
        CaptureSocketError(operation);
    }
#endif

    bool EnsureSockets() {
#if defined(_WIN32)
        static std::once_flag once;
        static int startupResult = 0;
        std::call_once(once, [] {
            WSADATA data{};
            startupResult = WSAStartup(MAKEWORD(2, 2), &data);
        });
        if (startupResult != 0) {
            lastNetworkError = "WSAStartup failed with error " +
                std::to_string(startupResult);
            return false;
        }
#endif
        return true;
    }

    SocketState* NewState(
        NativeSocket socket, bool associateCompletion = true) {
#if defined(_WIN32)
        if (associateCompletion) {
            int associationError = 0;
            if (!Absolute::RuntimeDetail::AssociateSocketCompletion(
                    socket, associationError)) {
                CaptureCompletionError(
                    "associate socket with IOCP", associationError);
                CloseNative(socket);
                return nullptr;
            }
        }
#else
        (void)associateCompletion;
#endif
        SocketState* state = new (std::nothrow) SocketState;
        if (!state) {
            CloseNative(socket);
            lastNetworkError = "socket handle allocation failed";
            return nullptr;
        }
        state->socket = socket;
        lastNetworkError.clear();
        return state;
    }

    SocketState* State(void* handle) {
        return static_cast<SocketState*>(handle);
    }

    bool Valid(SocketState* state) {
        if (state && state->socket != InvalidSocket) return true;
        lastNetworkError = "socket is closed";
        return false;
    }

    NativeSocket CreateNativeSocket(
        int family, int type, int protocol) {
#if defined(_WIN32)
        return WSASocketW(
            family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
#else
        return socket(family, type, protocol);
#endif
    }

    bool SetNonBlocking(NativeSocket socket) {
#if defined(__linux__) || defined(__APPLE__)
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#else
        (void)socket;
        return true;
#endif
    }

    bool WouldBlock() {
#if defined(_WIN32)
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    bool WaitForSocket(
        SocketState* state, std::uint32_t events, const char* operation) {
#if defined(__linux__) || defined(__APPLE__)
        if (Absolute::RuntimeDetail::IsSchedulerTask() &&
            state) {
            if (Absolute::RuntimeDetail::WaitSocketReady(
                    state->socket, events,
                    state->timeoutMilliseconds)) {
                return true;
            }
            if (errno == ETIMEDOUT) {
                lastNetworkError =
                    std::string(operation) + ": operation timed out";
                return false;
            }
            CaptureSocketError(operation);
            return false;
        }

        pollfd descriptor{
            state->socket,
            static_cast<short>(
                ((events & Absolute::RuntimeDetail::SocketReadyRead) != 0
                    ? POLLIN : 0) |
                ((events & Absolute::RuntimeDetail::SocketReadyWrite) != 0
                    ? POLLOUT : 0)),
            0};
        int result = 0;
        do {
            result = poll(
                &descriptor, 1,
                state->timeoutMilliseconds > 0
                    ? state->timeoutMilliseconds : -1);
        } while (result < 0 && errno == EINTR);
        if (result > 0) return true;
        if (result == 0) {
            errno = ETIMEDOUT;
            lastNetworkError =
                std::string(operation) + ": operation timed out";
            return false;
        }
        CaptureSocketError(operation);
        return false;
#else
        (void)state;
        (void)events;
        (void)operation;
        return false;
#endif
    }

    addrinfo* Resolve(const char* host, std::int32_t port, bool passive) {
        if (!EnsureSockets()) return nullptr;
        if (port < 0 || port > 65535) {
            lastNetworkError = "TCP port must be in [0, 65535]";
            return nullptr;
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        const std::string service = std::to_string(port);
        addrinfo* result = nullptr;
        const char* node = host && *host ? host : nullptr;
        const int status = getaddrinfo(node, service.c_str(), &hints, &result);
        if (status != 0) {
#if defined(_WIN32)
            lastNetworkError = "address resolution failed with error " +
                std::to_string(status);
#else
            lastNetworkError = std::string("address resolution failed: ") +
                gai_strerror(status);
#endif
            return nullptr;
        }
        return result;
    }

    addrinfo* ResolveForConnect(const char* host, std::int32_t port) {
        // Name resolution remains the portable blocking part. The resulting
        // address list is consumed by the scheduler thread after this fiber is
        // resumed, while connect itself uses the native socket reactor.
        return RunNetworkIo<addrinfo*>([&] {
            return Resolve(host, port, false);
        });
    }

    void SetReuseAddress(NativeSocket socket) {
        const int enabled = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
#if defined(_WIN32)
            reinterpret_cast<const char*>(&enabled),
            static_cast<int>(sizeof(enabled)));
#else
            &enabled,
            static_cast<socklen_t>(sizeof(enabled)));
#endif
    }

#if defined(_WIN32)
    bool UseNativeSocketCompletion(SocketState* state) {
        return state &&
            Absolute::RuntimeDetail::IsSchedulerTask();
    }

    struct WindowsAcceptOperation {
        LPFN_ACCEPTEX acceptEx = nullptr;
        SOCKET listener = INVALID_SOCKET;
        SOCKET accepted = INVALID_SOCKET;
        DWORD bytesReceived = 0;
        char addresses[2 * (sizeof(sockaddr_storage) + 16)]{};
    };

    int StartWindowsAccept(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsAcceptOperation*>(opaque);
        const BOOL started = operation->acceptEx(
            operation->listener, operation->accepted,
            operation->addresses, 0,
            static_cast<DWORD>(sizeof(sockaddr_storage) + 16),
            static_cast<DWORD>(sizeof(sockaddr_storage) + 16),
            &operation->bytesReceived, overlapped);
        return started ? 0 : SOCKET_ERROR;
    }

    LPFN_ACCEPTEX LoadAcceptEx(SOCKET listener) {
        GUID identifier = WSAID_ACCEPTEX;
        LPFN_ACCEPTEX result = nullptr;
        DWORD bytes = 0;
        if (WSAIoctl(
                listener, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &identifier, sizeof(identifier),
                &result, sizeof(result), &bytes,
                nullptr, nullptr) == SOCKET_ERROR) {
            CaptureSocketError("load AcceptEx");
            return nullptr;
        }
        return result;
    }

    LPFN_CONNECTEX LoadConnectEx(SOCKET socket) {
        GUID identifier = WSAID_CONNECTEX;
        LPFN_CONNECTEX result = nullptr;
        DWORD bytes = 0;
        if (WSAIoctl(
                socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &identifier, sizeof(identifier),
                &result, sizeof(result), &bytes,
                nullptr, nullptr) == SOCKET_ERROR) {
            CaptureSocketError("load ConnectEx");
            return nullptr;
        }
        return result;
    }

    bool BindConnectSocket(SOCKET socket, int family) {
        if (family == AF_INET) {
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = 0;
            return bind(
                socket, reinterpret_cast<sockaddr*>(&local),
                sizeof(local)) == 0;
        }
        if (family == AF_INET6) {
            sockaddr_in6 local{};
            local.sin6_family = AF_INET6;
            local.sin6_addr = in6addr_any;
            local.sin6_port = 0;
            return bind(
                socket, reinterpret_cast<sockaddr*>(&local),
                sizeof(local)) == 0;
        }
        WSASetLastError(WSAEAFNOSUPPORT);
        return false;
    }

    struct WindowsConnectOperation {
        LPFN_CONNECTEX connectEx = nullptr;
        SOCKET socket = INVALID_SOCKET;
        const sockaddr* address = nullptr;
        int addressLength = 0;
    };

    int StartWindowsConnect(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsConnectOperation*>(opaque);
        const BOOL started = operation->connectEx(
            operation->socket, operation->address,
            operation->addressLength, nullptr, 0,
            nullptr, overlapped);
        return started ? 0 : SOCKET_ERROR;
    }

    int SocketFamily(SOCKET socket) {
        sockaddr_storage address{};
        int length = sizeof(address);
        if (getsockname(
                socket, reinterpret_cast<sockaddr*>(&address),
                &length) != 0) {
            CaptureSocketError("get socket family");
            return AF_UNSPEC;
        }
        return address.ss_family;
    }

    struct WindowsSendOperation {
        SOCKET socket = INVALID_SOCKET;
        WSABUF buffer{};
    };

    int StartWindowsSend(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsSendOperation*>(opaque);
        return WSASend(
            operation->socket, &operation->buffer, 1,
            nullptr, 0, overlapped, nullptr);
    }

    struct WindowsReceiveOperation {
        SOCKET socket = INVALID_SOCKET;
        WSABUF buffer{};
        DWORD flags = 0;
    };

    int StartWindowsReceive(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsReceiveOperation*>(opaque);
        return WSARecv(
            operation->socket, &operation->buffer, 1,
            nullptr, &operation->flags, overlapped, nullptr);
    }

    struct WindowsSendToOperation {
        SOCKET socket = INVALID_SOCKET;
        WSABUF buffer{};
        sockaddr_in destination{};
    };

    int StartWindowsSendTo(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsSendToOperation*>(opaque);
        return WSASendTo(
            operation->socket, &operation->buffer, 1,
            nullptr, 0,
            reinterpret_cast<const sockaddr*>(&operation->destination),
            sizeof(operation->destination), overlapped, nullptr);
    }

    struct WindowsReceiveFromOperation {
        SOCKET socket = INVALID_SOCKET;
        WSABUF buffer{};
        DWORD flags = 0;
        sockaddr_in source{};
        int sourceLength = sizeof(source);
    };

    int StartWindowsReceiveFrom(void* opaque, OVERLAPPED* overlapped) {
        auto* operation =
            static_cast<WindowsReceiveFromOperation*>(opaque);
        return WSARecvFrom(
            operation->socket, &operation->buffer, 1,
            nullptr, &operation->flags,
            reinterpret_cast<sockaddr*>(&operation->source),
            &operation->sourceLength, overlapped, nullptr);
    }
#endif
}

extern "C" void* absolute_net_tcp_connect(const char* host, std::int32_t port) {
    if (!host || !*host) {
        lastNetworkError = "remote host is empty";
        return nullptr;
    }
    addrinfo* addresses = ResolveForConnect(host, port);
    if (!addresses) return nullptr;

    const bool schedulerTask =
        Absolute::RuntimeDetail::IsSchedulerTask();
    NativeSocket connected = InvalidSocket;
    bool connectedAlreadyAssociated = false;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        NativeSocket candidate = CreateNativeSocket(address->ai_family,
            address->ai_socktype, address->ai_protocol);
        if (candidate == InvalidSocket) {
            CaptureSocketError("socket");
            continue;
        }

#if defined(_WIN32)
        if (schedulerTask) {
            LPFN_CONNECTEX connectEx = LoadConnectEx(candidate);
            if (!connectEx ||
                !BindConnectSocket(candidate, address->ai_family)) {
                if (connectEx) CaptureSocketError("bind ConnectEx socket");
                CloseNative(candidate);
                continue;
            }
            int associationError = 0;
            if (!Absolute::RuntimeDetail::AssociateSocketCompletion(
                    candidate, associationError)) {
                CaptureCompletionError(
                    "associate ConnectEx socket with IOCP",
                    associationError);
                CloseNative(candidate);
                continue;
            }
            WindowsConnectOperation operation{
                connectEx, candidate, address->ai_addr,
                static_cast<int>(address->ai_addrlen)};
            Absolute::RuntimeDetail::SocketCompletionResult completion;
            if (!Absolute::RuntimeDetail::RunSocketCompletion(
                    candidate, &StartWindowsConnect,
                    &operation, completion)) {
                CaptureCompletionError("ConnectEx", completion.error);
                CloseNative(candidate);
                continue;
            }
            if (setsockopt(
                    candidate, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
                    nullptr, 0) != 0) {
                CaptureSocketError("update connected socket context");
                CloseNative(candidate);
                continue;
            }
            connectedAlreadyAssociated = true;
            connected = candidate;
            break;
        }
#else
        if (schedulerTask && !SetNonBlocking(candidate)) {
            CaptureSocketError("set connect socket nonblocking");
            CloseNative(candidate);
            continue;
        }
#endif

        if (::connect(candidate, address->ai_addr,
                static_cast<int>(address->ai_addrlen)) == 0) {
            connected = candidate;
            break;
        }

#if defined(__linux__) || defined(__APPLE__)
        if (schedulerTask && errno == EINPROGRESS) {
            SocketState pending{candidate, 0};
            if (WaitForSocket(
                    &pending,
                    Absolute::RuntimeDetail::SocketReadyWrite,
                    "connect")) {
                int socketError = 0;
                socklen_t errorLength =
                    static_cast<socklen_t>(sizeof(socketError));
                if (getsockopt(
                        candidate, SOL_SOCKET, SO_ERROR,
                        &socketError, &errorLength) == 0 &&
                    socketError == 0) {
                    connected = candidate;
                    break;
                }
                if (socketError != 0) errno = socketError;
            }
        }
#endif
        CaptureSocketError("connect");
        CloseNative(candidate);
    }
    freeaddrinfo(addresses);
    if (connected == InvalidSocket) return nullptr;
    if (!schedulerTask && !SetNonBlocking(connected)) {
        CaptureSocketError("set nonblocking");
        CloseNative(connected);
        return nullptr;
    }
    return NewState(connected, !connectedAlreadyAssociated);
}

extern "C" void* absolute_net_tcp_listen(
    const char* host, std::int32_t port, std::int32_t backlog) {
    return RunNetworkIo<void*>([&]() -> void* {
        addrinfo* addresses = Resolve(host, port, true);
        if (!addresses) return nullptr;
        NativeSocket listener = InvalidSocket;
        for (addrinfo* address = addresses; address; address = address->ai_next) {
            NativeSocket candidate = CreateNativeSocket(address->ai_family,
                address->ai_socktype, address->ai_protocol);
            if (candidate == InvalidSocket) {
                CaptureSocketError("socket");
                continue;
            }
            SetReuseAddress(candidate);
            if (bind(candidate, address->ai_addr,
                    static_cast<int>(address->ai_addrlen)) != 0) {
                CaptureSocketError("bind");
                CloseNative(candidate);
                continue;
            }
            if (listen(candidate, (std::max)(1, backlog)) != 0) {
                CaptureSocketError("listen");
                CloseNative(candidate);
                continue;
            }
            listener = candidate;
            break;
        }
        freeaddrinfo(addresses);
        if (listener == InvalidSocket) return nullptr;
        if (!SetNonBlocking(listener)) {
            CaptureSocketError("set nonblocking");
            CloseNative(listener);
            return nullptr;
        }
        return NewState(listener);
    });
}

extern "C" void* absolute_net_tcp_accept(void* handle) {
    SocketState* listener = State(handle);
    return RunSocketIo<void*>(listener, [&]() -> void* {
        if (!Valid(listener)) return nullptr;
#if defined(_WIN32)
        if (UseNativeSocketCompletion(listener)) {
            LPFN_ACCEPTEX acceptEx = LoadAcceptEx(listener->socket);
            if (!acceptEx) return nullptr;
            const int family = SocketFamily(listener->socket);
            if (family == AF_UNSPEC) return nullptr;
            NativeSocket accepted =
                CreateNativeSocket(family, SOCK_STREAM, IPPROTO_TCP);
            if (accepted == InvalidSocket) {
                CaptureSocketError("create accepted socket");
                return nullptr;
            }
            WindowsAcceptOperation operation{
                acceptEx, listener->socket, accepted};
            Absolute::RuntimeDetail::SocketCompletionResult completion;
            if (!Absolute::RuntimeDetail::RunSocketCompletion(
                    listener->socket, &StartWindowsAccept,
                    &operation, completion,
                    listener->timeoutMilliseconds)) {
                CaptureCompletionError("AcceptEx", completion.error);
                CloseNative(accepted);
                return nullptr;
            }
            if (setsockopt(
                    accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                    reinterpret_cast<const char*>(&listener->socket),
                    sizeof(listener->socket)) != 0) {
                CaptureSocketError("update accepted socket context");
                CloseNative(accepted);
                return nullptr;
            }
            return NewState(accepted);
        }
#endif
        while (true) {
            NativeSocket accepted =
                accept(listener->socket, nullptr, nullptr);
            if (accepted != InvalidSocket) {
                if (!SetNonBlocking(accepted)) {
                    CaptureSocketError("set accepted socket nonblocking");
                    CloseNative(accepted);
                    return nullptr;
                }
                return NewState(accepted);
            }
#if defined(__linux__) || defined(__APPLE__)
            if (WouldBlock()) {
                if (WaitForSocket(
                        listener,
                        Absolute::RuntimeDetail::SocketReadyRead,
                        "accept")) {
                    continue;
                }
                return nullptr;
            }
#endif
            CaptureSocketError("accept");
            return nullptr;
        }
    });
}

extern "C" std::int32_t absolute_net_tcp_send(void* handle, const char* text) {
    SocketState* state = State(handle);
    return RunSocketIo<std::int32_t>(state, [&] {
        if (!Valid(state) || !text) {
            if (!text) lastNetworkError = "send text is null";
            return std::int32_t{-1};
        }
        const std::size_t length = std::strlen(text);
        std::size_t sent = 0;
        while (sent < length) {
            const std::size_t remaining = length - sent;
            const int chunk = static_cast<int>(
                std::min<std::size_t>(remaining, 1U << 20));
#if defined(_WIN32)
            int result = 0;
            if (UseNativeSocketCompletion(state)) {
                WindowsSendOperation operation{
                    state->socket,
                    WSABUF{
                        static_cast<ULONG>(chunk),
                        const_cast<char*>(text + sent)}};
                Absolute::RuntimeDetail::SocketCompletionResult completion;
                if (!Absolute::RuntimeDetail::RunSocketCompletion(
                        state->socket, &StartWindowsSend,
                        &operation, completion,
                        state->timeoutMilliseconds)) {
                    CaptureCompletionError("WSASend", completion.error);
                    return std::int32_t{-1};
                }
                result = static_cast<int>(completion.bytesTransferred);
            }
            else {
                result = send(
                    state->socket, text + sent, chunk, 0);
            }
#else
            const int result = static_cast<int>(send(state->socket, text + sent,
                static_cast<std::size_t>(chunk), MSG_NOSIGNAL));
#endif
            if (result < 0) {
#if defined(__linux__) || defined(__APPLE__)
                if (WouldBlock()) {
                    if (WaitForSocket(
                            state,
                            Absolute::RuntimeDetail::SocketReadyWrite,
                            "send")) {
                        continue;
                    }
                    return std::int32_t{-1};
                }
#endif
                CaptureSocketError("send");
                return std::int32_t{-1};
            }
            if (result == 0) {
                lastNetworkError = "send: socket closed before completion";
                return std::int32_t{-1};
            }
            sent += static_cast<std::size_t>(result);
        }
        lastNetworkError.clear();
        return static_cast<std::int32_t>(
            std::min<std::size_t>(sent, INT32_MAX));
    });
}

extern "C" const char* absolute_net_tcp_receive(
    void* handle, std::int32_t maximumBytes) {
    SocketState* state = State(handle);
    return RunSocketIo<const char*>(state, [&]() -> const char* {
        if (!Valid(state)) return nullptr;
        if (maximumBytes <= 0 || maximumBytes > 16 * 1024 * 1024) {
            lastNetworkError = "receive size must be in [1, 16777216]";
            return nullptr;
        }
        std::string receiveBuffer(
            static_cast<std::size_t>(maximumBytes), '\0');
        int count = 0;
        while (true) {
#if defined(_WIN32)
            if (UseNativeSocketCompletion(state)) {
                WindowsReceiveOperation operation{
                    state->socket,
                    WSABUF{
                        static_cast<ULONG>(maximumBytes),
                        receiveBuffer.data()}};
                Absolute::RuntimeDetail::SocketCompletionResult completion;
                if (!Absolute::RuntimeDetail::RunSocketCompletion(
                        state->socket, &StartWindowsReceive,
                        &operation, completion,
                        state->timeoutMilliseconds)) {
                    CaptureCompletionError("WSARecv", completion.error);
                    return nullptr;
                }
                count = static_cast<int>(completion.bytesTransferred);
            }
            else {
                count = recv(
                    state->socket, receiveBuffer.data(),
                    maximumBytes, 0);
            }
#else
            count = static_cast<int>(recv(state->socket,
                receiveBuffer.data(),
                static_cast<std::size_t>(maximumBytes), 0));
#endif
            if (count >= 0) break;
#if defined(__linux__) || defined(__APPLE__)
            if (WouldBlock()) {
                if (WaitForSocket(
                        state,
                        Absolute::RuntimeDetail::SocketReadyRead,
                        "receive")) {
                    continue;
                }
                return nullptr;
            }
#endif
            CaptureSocketError("receive");
            return nullptr;
        }
        receiveBuffer.resize(static_cast<std::size_t>(count));
        lastNetworkError.clear();
        // Durable copy: callers may keep the string after the next
        // receive/close and after the I/O worker processes another request.
        const std::size_t size = receiveBuffer.size() + 1;
        char* durable = static_cast<char*>(std::malloc(size));
        if (!durable) {
            lastNetworkError = "receive allocation failed";
            return nullptr;
        }
        std::memcpy(durable, receiveBuffer.c_str(), size);
        return durable;
    });
}

extern "C" std::int32_t absolute_net_tcp_set_timeout(
    void* handle, std::int32_t milliseconds) {
    SocketState* state = State(handle);
    if (!Valid(state) || milliseconds < 0) {
        if (milliseconds < 0) lastNetworkError = "timeout cannot be negative";
        return 0;
    }
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    const char* value = reinterpret_cast<const char*>(&timeout);
    const int length = static_cast<int>(sizeof(timeout));
#else
    const timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    const void* value = &timeout;
    const socklen_t length = static_cast<socklen_t>(sizeof(timeout));
#endif
    if (setsockopt(state->socket, SOL_SOCKET, SO_RCVTIMEO,
#if defined(_WIN32)
            value, length
#else
            value, length
#endif
        ) != 0 ||
        setsockopt(state->socket, SOL_SOCKET, SO_SNDTIMEO,
#if defined(_WIN32)
            value, length
#else
            value, length
#endif
        ) != 0) {
        CaptureSocketError("set timeout");
        return 0;
    }
    state->timeoutMilliseconds = milliseconds;
    lastNetworkError.clear();
    return 1;
}

extern "C" std::int32_t absolute_net_tcp_port(void* handle) {
    SocketState* state = State(handle);
    if (!Valid(state)) return -1;
    sockaddr_storage address{};
#if defined(_WIN32)
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (getsockname(state->socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        CaptureSocketError("get local port");
        return -1;
    }
    if (address.ss_family == AF_INET)
        return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
    if (address.ss_family == AF_INET6)
        return ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);
    lastNetworkError = "socket has an unsupported address family";
    return -1;
}

extern "C" void absolute_net_tcp_shutdown(void* handle) {
    SocketState* state = State(handle);
    if (!state || state->socket == InvalidSocket) return;
#if defined(_WIN32)
    shutdown(state->socket, SD_BOTH);
#else
    shutdown(state->socket, SHUT_RDWR);
#endif
}

extern "C" void absolute_net_tcp_close(void* handle) {
    SocketState* state = State(handle);
    if (!state) return;
    CloseNative(state->socket);
    state->socket = InvalidSocket;
    delete state;
}

extern "C" const char* absolute_net_resolve_host(const char* hostname) {
    return RunNetworkIo<const char*>([&]() -> const char* {
        if (!EnsureSockets() || !hostname || !*hostname) {
            lastNetworkError = "hostname is empty";
            return "";
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        int status = getaddrinfo(hostname, nullptr, &hints, &result);
        if (status != 0 || !result) {
            lastNetworkError =
                "Failed to resolve hostname: " + std::string(hostname);
            return "";
        }
        char ipBuffer[INET_ADDRSTRLEN] = {0};
        sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipBuffer, sizeof(ipBuffer));
        freeaddrinfo(result);
        // Durable copy: Absolute string is a bare pointer and must outlive later
        // resolve/send calls on both the scheduler and I/O threads.
        const std::size_t size = std::strlen(ipBuffer) + 1;
        char* durable = static_cast<char*>(std::malloc(size));
        if (!durable) {
            lastNetworkError = "resolve host allocation failed";
            return "";
        }
        std::memcpy(durable, ipBuffer, size);
        lastNetworkError.clear();
        return durable;
    });
}

extern "C" void* absolute_net_udp_bind(const char* host, std::int32_t port) {
    if (!EnsureSockets()) return nullptr;
    NativeSocket sock =
        CreateNativeSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == InvalidSocket) {
        CaptureSocketError("udp socket creation");
        return nullptr;
    }
    SetReuseAddress(sock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host && *host && std::strcmp(host, "0.0.0.0") != 0 && std::strcmp(host, "*") != 0) {
        inet_pton(AF_INET, host, &addr.sin_addr);
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CaptureSocketError("udp bind");
        CloseNative(sock);
        return nullptr;
    }
    if (!SetNonBlocking(sock)) {
        CaptureSocketError("set udp socket nonblocking");
        CloseNative(sock);
        return nullptr;
    }
    return NewState(sock);
}

extern "C" std::int32_t absolute_net_udp_send_to(void* handle, const char* host, std::int32_t port, const char* text) {
    SocketState* state = State(handle);
    return RunSocketIo<std::int32_t>(state, [&] {
        if (!Valid(state) || !text || !host || !*host) {
            lastNetworkError = "invalid arguments for udp send_to";
            return std::int32_t{-1};
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            const char* resolved = absolute_net_resolve_host(host);
            if (!resolved || !*resolved ||
                inet_pton(AF_INET, resolved, &addr.sin_addr) <= 0) {
                lastNetworkError = "failed to resolve host for udp send_to";
                return std::int32_t{-1};
            }
        }
        const std::size_t len = std::strlen(text);
        int sent = 0;
        while (true) {
#if defined(_WIN32)
            if (UseNativeSocketCompletion(state)) {
                WindowsSendToOperation operation{
                    state->socket,
                    WSABUF{
                        static_cast<ULONG>(len),
                        const_cast<char*>(text)},
                    addr};
                Absolute::RuntimeDetail::SocketCompletionResult completion;
                if (!Absolute::RuntimeDetail::RunSocketCompletion(
                        state->socket, &StartWindowsSendTo,
                        &operation, completion,
                        state->timeoutMilliseconds)) {
                    CaptureCompletionError(
                        "WSASendTo", completion.error);
                    return std::int32_t{-1};
                }
                sent = static_cast<int>(completion.bytesTransferred);
            }
            else {
                sent = sendto(state->socket, text,
                    static_cast<int>(len), 0,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            }
#else
            sent = sendto(state->socket, text,
                static_cast<int>(len), 0,
                reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif
            if (sent >= 0) break;
#if defined(__linux__) || defined(__APPLE__)
            if (WouldBlock()) {
                if (WaitForSocket(
                        state,
                        Absolute::RuntimeDetail::SocketReadyWrite,
                        "udp sendto")) {
                    continue;
                }
                return std::int32_t{-1};
            }
#endif
            CaptureSocketError("udp sendto");
            return std::int32_t{-1};
        }
        lastNetworkError.clear();
        return static_cast<std::int32_t>(sent);
    });
}

extern "C" const char* absolute_net_udp_receive_from(void* handle, std::int32_t maxBytes) {
    SocketState* state = State(handle);
    return RunSocketIo<const char*>(state, [&]() -> const char* {
        if (!Valid(state) || maxBytes <= 0) {
            lastNetworkError = "invalid receive_from parameters";
            return nullptr;
        }
        std::string receiveBuffer(
            static_cast<std::size_t>(maxBytes), '\0');
        sockaddr_in srcAddr{};
#if defined(_WIN32)
        int addrLen = sizeof(srcAddr);
#else
        socklen_t addrLen = sizeof(srcAddr);
#endif
        int count = 0;
        while (true) {
#if defined(_WIN32)
            if (UseNativeSocketCompletion(state)) {
                WindowsReceiveFromOperation operation{
                    state->socket,
                    WSABUF{
                        static_cast<ULONG>(maxBytes),
                        receiveBuffer.data()}};
                Absolute::RuntimeDetail::SocketCompletionResult completion;
                if (!Absolute::RuntimeDetail::RunSocketCompletion(
                        state->socket, &StartWindowsReceiveFrom,
                        &operation, completion,
                        state->timeoutMilliseconds)) {
                    CaptureCompletionError(
                        "WSARecvFrom", completion.error);
                    return nullptr;
                }
                count = static_cast<int>(completion.bytesTransferred);
                srcAddr = operation.source;
                addrLen = operation.sourceLength;
            }
            else {
                count = recvfrom(state->socket,
                    receiveBuffer.data(), maxBytes, 0,
                    reinterpret_cast<sockaddr*>(&srcAddr), &addrLen);
            }
#else
            count = recvfrom(state->socket,
                receiveBuffer.data(), maxBytes, 0,
                reinterpret_cast<sockaddr*>(&srcAddr), &addrLen);
#endif
            if (count >= 0) break;
#if defined(__linux__) || defined(__APPLE__)
            if (WouldBlock()) {
                if (WaitForSocket(
                        state,
                        Absolute::RuntimeDetail::SocketReadyRead,
                        "udp recvfrom")) {
                    continue;
                }
                return nullptr;
            }
#endif
            CaptureSocketError("udp recvfrom");
            return nullptr;
        }
        receiveBuffer.resize(static_cast<std::size_t>(count));
        lastNetworkError.clear();
        const std::size_t size = receiveBuffer.size() + 1;
        char* durable = static_cast<char*>(std::malloc(size));
        if (!durable) {
            lastNetworkError = "udp receive allocation failed";
            return nullptr;
        }
        std::memcpy(durable, receiveBuffer.c_str(), size);
        return durable;
    });
}

extern "C" void absolute_net_udp_close(void* handle) {
    absolute_net_tcp_close(handle);
}

extern "C" const char* absolute_net_error() {
    return lastNetworkError.c_str();
}
