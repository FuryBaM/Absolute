#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" std::uint64_t absolute_managed_create(std::uint64_t size);
extern "C" void* absolute_managed_get(std::uint64_t handle);
extern "C" void absolute_managed_destroy(std::uint64_t handle);
extern "C" std::uint64_t absolute_managed_transfer(std::uint64_t handle);

namespace {
    constexpr std::uint32_t TestGenerationLimit = 4;

    std::uint32_t HandleId(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle);
    }

    std::uint32_t HandleGeneration(std::uint64_t handle) {
        return static_cast<std::uint32_t>(handle >> 32U);
    }

    void Require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    }
}

int main() {
    // Destroy/reallocate the same slot until its last representable generation.
    // The next destroy must retire it rather than wrap to generation one.
    std::uint64_t handle = absolute_managed_create(8);
    const std::uint64_t oldest = handle;
    const std::uint32_t firstId = HandleId(handle);
    Require(HandleGeneration(handle) == 1, "fresh handle did not start at generation one");

    for (std::uint32_t generation = 1;
         generation < TestGenerationLimit; ++generation) {
        absolute_managed_destroy(handle);
        Require(absolute_managed_get(handle) == nullptr,
            "destroyed handle remained valid before generation exhaustion");
        handle = absolute_managed_create(8);
        Require(HandleId(handle) == firstId,
            "non-exhausted slot was not reused");
        Require(HandleGeneration(handle) == generation + 1,
            "reused slot did not advance its generation");
    }

    absolute_managed_destroy(handle);
    Require(absolute_managed_get(oldest) == nullptr,
        "oldest stale handle resurrected after generation exhaustion");
    const std::uint64_t afterRetire = absolute_managed_create(8);
    Require(HandleId(afterRetire) != firstId,
        "exhausted slot was reused instead of retired");
    absolute_managed_destroy(afterRetire);

    // Transfer rotates a live owner's generation. At exhaustion it cannot wrap
    // either, so the runtime must move the table entry while leaving the object
    // allocation itself at the same address.
    std::uint64_t transferred = absolute_managed_create(16);
    void* pointee = absolute_managed_get(transferred);
    Require(pointee != nullptr, "transfer test allocation failed");

    while (HandleGeneration(transferred) < TestGenerationLimit) {
        const std::uint64_t previous = transferred;
        transferred = absolute_managed_transfer(previous);
        Require(absolute_managed_get(previous) == nullptr,
            "transfer did not invalidate the previous generation");
        Require(absolute_managed_get(transferred) == pointee,
            "ordinary transfer changed the pointee");
    }

    const std::uint64_t exhausted = transferred;
    const std::uint32_t exhaustedId = HandleId(exhausted);
    transferred = absolute_managed_transfer(exhausted);
    Require(HandleId(transferred) != exhaustedId,
        "exhausted transfer wrapped instead of rehoming the slot");
    Require(absolute_managed_get(exhausted) == nullptr,
        "exhausted sender handle remained valid after rehome");
    Require(absolute_managed_get(transferred) == pointee,
        "generation-exhausted transfer moved or lost the pointee");

    absolute_managed_destroy(transferred);
    std::cout << "runtime-managed-generation-wrap=ok\n";
    return 0;
}
