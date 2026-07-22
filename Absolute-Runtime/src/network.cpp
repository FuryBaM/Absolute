#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
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
        std::string receiveBuffer;
    };

    thread_local std::string lastNetworkError;

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

    SocketState* NewState(NativeSocket socket) {
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
}

extern "C" void* absolute_net_tcp_connect(const char* host, std::int32_t port) {
    if (!host || !*host) {
        lastNetworkError = "remote host is empty";
        return nullptr;
    }
    addrinfo* addresses = Resolve(host, port, false);
    if (!addresses) return nullptr;
    NativeSocket connected = InvalidSocket;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        NativeSocket candidate = socket(address->ai_family,
            address->ai_socktype, address->ai_protocol);
        if (candidate == InvalidSocket) {
            CaptureSocketError("socket");
            continue;
        }
        if (::connect(candidate, address->ai_addr,
            static_cast<int>(address->ai_addrlen)) == 0) {
            connected = candidate;
            break;
        }
        CaptureSocketError("connect");
        CloseNative(candidate);
    }
    freeaddrinfo(addresses);
    if (connected == InvalidSocket) {
        return nullptr;
    }
    return NewState(connected);
}

extern "C" void* absolute_net_tcp_listen(
    const char* host, std::int32_t port, std::int32_t backlog) {
    addrinfo* addresses = Resolve(host, port, true);
    if (!addresses) return nullptr;
    NativeSocket listener = InvalidSocket;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        NativeSocket candidate = socket(address->ai_family,
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
    if (listener == InvalidSocket) {
        return nullptr;
    }
    return NewState(listener);
}

extern "C" void* absolute_net_tcp_accept(void* handle) {
    SocketState* listener = State(handle);
    if (!Valid(listener)) return nullptr;
    NativeSocket accepted = accept(listener->socket, nullptr, nullptr);
    if (accepted == InvalidSocket) {
        CaptureSocketError("accept");
        return nullptr;
    }
    return NewState(accepted);
}

extern "C" std::int32_t absolute_net_tcp_send(void* handle, const char* text) {
    SocketState* state = State(handle);
    if (!Valid(state) || !text) {
        if (!text) lastNetworkError = "send text is null";
        return -1;
    }
    const std::size_t length = std::strlen(text);
    std::size_t sent = 0;
    while (sent < length) {
        const std::size_t remaining = length - sent;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 1U << 20));
#if defined(_WIN32)
        const int result = send(state->socket, text + sent, chunk, 0);
#else
        const int result = static_cast<int>(send(state->socket, text + sent,
            static_cast<std::size_t>(chunk), MSG_NOSIGNAL));
#endif
        if (result <= 0) {
            CaptureSocketError("send");
            return -1;
        }
        sent += static_cast<std::size_t>(result);
    }
    lastNetworkError.clear();
    return static_cast<std::int32_t>(std::min<std::size_t>(sent, INT32_MAX));
}

extern "C" const char* absolute_net_tcp_receive(void* handle, std::int32_t maximumBytes) {
    SocketState* state = State(handle);
    if (!Valid(state)) return nullptr;
    if (maximumBytes <= 0 || maximumBytes > 16 * 1024 * 1024) {
        lastNetworkError = "receive size must be in [1, 16777216]";
        return nullptr;
    }
    state->receiveBuffer.resize(static_cast<std::size_t>(maximumBytes));
#if defined(_WIN32)
    const int count = recv(state->socket, state->receiveBuffer.data(), maximumBytes, 0);
#else
    const int count = static_cast<int>(recv(state->socket,
        state->receiveBuffer.data(), static_cast<std::size_t>(maximumBytes), 0));
#endif
    if (count < 0) {
        state->receiveBuffer.clear();
        CaptureSocketError("receive");
        return nullptr;
    }
    state->receiveBuffer.resize(static_cast<std::size_t>(count));
    lastNetworkError.clear();
    return state->receiveBuffer.c_str();
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

extern "C" const char* absolute_net_error() {
    return lastNetworkError.c_str();
}
