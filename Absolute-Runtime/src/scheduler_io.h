#pragma once

#include <type_traits>
#include <utility>

namespace Absolute::RuntimeDetail {
    using BlockingIoOperation = void (*)(void*);
    using IoCompletion = void (*)(void*);
    using IoRegistration = void (*)(void*, IoCompletion, void*);

    bool IsSchedulerTask() noexcept;
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
