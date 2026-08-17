#include <cstdint>
#include <cstdio>
#include <string>

extern "C" void absolute_managed_destroy(std::uint64_t handle);

namespace {
    struct ErrorState {
        std::uint64_t handle = 0;
        std::uint64_t type = 0;
        // Recorded at the throw site, where the static type and the Error's
        // own message are both in hand. Without them a report could only name
        // the hashed type identifier, which told a user nothing about what
        // went wrong.
        std::string typeName;
        std::string message;
    };

    thread_local ErrorState currentError;
}

extern "C" void absolute_error_set_details(const char* typeName, const char* message) {
    currentError.typeName = typeName ? typeName : "";
    currentError.message = message ? message : "";
}

extern "C" const char* absolute_error_type_name() {
    return currentError.typeName.c_str();
}

extern "C" const char* absolute_error_message() {
    return currentError.message.c_str();
}

extern "C" void absolute_error_set(std::uint64_t handle, std::uint64_t type) {
    if (currentError.handle && currentError.handle != handle)
        absolute_managed_destroy(currentError.handle);
    currentError.handle = handle;
    currentError.type = type;
}

extern "C" bool absolute_error_pending() {
    return currentError.handle != 0;
}

extern "C" std::uint64_t absolute_error_type() {
    return currentError.type;
}

extern "C" std::uint64_t absolute_error_take() {
    const std::uint64_t handle = currentError.handle;
    currentError.handle = 0;
    currentError.type = 0;
    return handle;
}

extern "C" void absolute_error_discard() {
    if (currentError.handle) absolute_managed_destroy(currentError.handle);
    currentError = {};
}

extern "C" void absolute_error_report() {
    // The type identifier is a hash of the type name and means nothing to the
    // person reading the output, so it is the fallback rather than the report.
    if (!currentError.typeName.empty() && !currentError.message.empty())
        std::fprintf(stderr, "Unhandled Absolute exception: %s: %s\n",
            currentError.typeName.c_str(), currentError.message.c_str());
    else if (!currentError.typeName.empty())
        std::fprintf(stderr, "Unhandled Absolute exception: %s\n",
            currentError.typeName.c_str());
    else
        std::fprintf(stderr, "Unhandled Absolute exception (type-id=%llu)\n",
            static_cast<unsigned long long>(currentError.type));
    absolute_error_discard();
}
