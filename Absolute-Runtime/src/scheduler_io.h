#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// Durable language strings live behind this allocator. Network and TLS I/O
// copy received bytes into it on every target, including Windows, so the
// declaration cannot sit inside a POSIX-only #else.
extern "C" char* absolute_string_alloc(std::size_t bytes);

namespace Absolute::RuntimeDetail {
    using BlockingIoOperation = void (*)(void*);
    using IoCompletion = void (*)(void*);
    using IoRegistration = void (*)(void*, IoCompletion, void*);

    bool IsSchedulerTask() noexcept;
    std::int32_t CurrentTaskDeadlineMilliseconds() noexcept;
    void RunBlockingIoImpl(BlockingIoOperation operation, void* context);
    void SuspendForIo(IoRegistration registration, void* context);

    template <class Operation>
    void RunBlockingIo(Operation&& operation) {
        using StoredOperation = std::decay_t<Operation>;
        StoredOperation stored(std::forward<Operation>(operation));
        RunBlockingIoImpl(
            [](void* context) {
                (*static_cast<StoredOperation*>(context))();
            },
            &stored);
    }
}
