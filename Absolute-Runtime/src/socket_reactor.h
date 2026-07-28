#pragma once

#include <cstdint>

namespace Absolute::RuntimeDetail {
    enum SocketReady : std::uint32_t {
        SocketReadyRead = 1U << 0U,
        SocketReadyWrite = 1U << 1U
    };

    // Suspends the current scheduler fiber until a one-shot native socket
    // readiness notification arrives. This is implemented with epoll on
    // Linux/Android; other platforms return ENOTSUP and keep using offload.
    bool WaitSocketReady(int descriptor, std::uint32_t events);
}
