#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
    [[noreturn]] void ArrayAllocationFailure(const char* message) {
        std::cerr << "Absolute runtime error: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

extern "C" void* absolute_array_calloc(
    std::int64_t count, std::uint64_t elementSize) {
    if (count < 0)
        ArrayAllocationFailure("array size must not be negative");

    const std::uint64_t elements = static_cast<std::uint64_t>(count);
    const std::uint64_t maximum =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());

    if (elementSize > maximum ||
        (elementSize != 0 && elements > maximum / elementSize))
        ArrayAllocationFailure("array allocation size overflow");

    const std::size_t total = static_cast<std::size_t>(
        elements * elementSize);
    // Zero-length heap arrays are still owners. Give them a distinct allocation
    // instead of inheriting the implementation-defined result of calloc(0, n),
    // because owner == null means "borrowed/static" everywhere else.
    void* memory = std::calloc(1, total == 0 ? 1 : total);
    if (!memory)
        ArrayAllocationFailure("array allocation failed");
    return memory;
}
