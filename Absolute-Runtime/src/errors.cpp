#include <cstdint>
#include <cstdio>

extern "C" void absolute_managed_destroy(std::uint64_t handle);

namespace {
    struct ErrorState {
        std::uint64_t handle = 0;
        std::uint64_t type = 0;
    };

    thread_local ErrorState currentError;
}

extern "C" void absolute_error_set(std::uint64_t handle, std::uint64_t type) {
    if (currentError.handle && currentError.handle != handle)
        absolute_managed_destroy(currentError.handle);
    currentError = {handle, type};
}

extern "C" bool absolute_error_pending() {
    return currentError.handle != 0;
}

extern "C" std::uint64_t absolute_error_type() {
    return currentError.type;
}

extern "C" std::uint64_t absolute_error_take() {
    const std::uint64_t handle = currentError.handle;
    currentError = {};
    return handle;
}

extern "C" void absolute_error_discard() {
    if (currentError.handle) absolute_managed_destroy(currentError.handle);
    currentError = {};
}

extern "C" void absolute_error_report() {
    std::fprintf(stderr, "Unhandled Absolute exception (type-id=%llu)\n",
        static_cast<unsigned long long>(currentError.type));
    absolute_error_discard();
}
