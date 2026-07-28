#pragma once

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

namespace Absolute::RuntimeDetail {
    enum SocketReady : std::uint32_t {
        SocketReadyRead = 1U << 0U,
        SocketReadyWrite = 1U << 1U
    };

    // Suspends the current scheduler fiber until a one-shot native socket
    // readiness notification arrives or the optional timeout expires. This is
    // implemented with epoll on Linux/Android and kqueue on macOS; unsupported
    // platforms return ENOTSUP.
    bool WaitSocketReady(
        int descriptor, std::uint32_t events,
        std::int32_t timeoutMilliseconds = 0);

#if defined(_WIN32)
    using SocketOperationStart =
        int (*)(void* context, OVERLAPPED* overlapped);

    struct SocketCompletionResult {
        std::uint32_t bytesTransferred = 0;
        int error = 0;
    };

    // Each socket is associated with the process-wide IOCP exactly once.
    bool AssociateSocketCompletion(SOCKET socket, int& error);

    // Starts one overlapped Winsock operation and suspends the current
    // scheduler fiber until its completion packet arrives.
    bool RunSocketCompletion(
        SOCKET socket, SocketOperationStart start, void* context,
        SocketCompletionResult& result,
        std::int32_t timeoutMilliseconds = 0);
#endif
}
