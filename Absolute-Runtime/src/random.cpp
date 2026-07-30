#include <bit>
#include <cstdint>
#include <new>
#include <random>

namespace {
    struct RandomState {
        std::uint64_t values[4]{};
    };

    std::uint64_t SplitMix64(std::uint64_t& state) {
        std::uint64_t value = (state += 0x9e3779b97f4a7c15ULL);
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    std::uint64_t Next(RandomState& state) {
        const std::uint64_t result = std::rotl(state.values[1] * 5, 7) * 9;
        const std::uint64_t shift = state.values[1] << 17;
        state.values[2] ^= state.values[0];
        state.values[3] ^= state.values[1];
        state.values[1] ^= state.values[2];
        state.values[0] ^= state.values[3];
        state.values[2] ^= shift;
        state.values[3] = std::rotl(state.values[3], 45);
        return result;
    }

    RandomState* State(std::uint64_t* handle) {
        return reinterpret_cast<RandomState*>(handle);
    }
}

extern "C" std::uint64_t* absolute_random_create(std::uint64_t seed) {
    RandomState* state = new (std::nothrow) RandomState;
    if (!state) return nullptr;
    for (std::uint64_t& value : state->values) value = SplitMix64(seed);
    return reinterpret_cast<std::uint64_t*>(state);
}

extern "C" void absolute_random_destroy(std::uint64_t* handle) {
    delete State(handle);
}

extern "C" std::uint64_t absolute_random_u64(std::uint64_t* handle) {
    return handle ? Next(*State(handle)) : 0;
}

extern "C" std::int32_t absolute_random_i32(std::uint64_t* handle) {
    const std::uint32_t bits = static_cast<std::uint32_t>(absolute_random_u64(handle) >> 32);
    return std::bit_cast<std::int32_t>(bits);
}

extern "C" std::int32_t absolute_random_range(
    std::uint64_t* handle, std::int32_t minimum, std::int32_t maximum) {
    if (!handle || minimum >= maximum) return minimum;
    const std::uint64_t span = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(maximum) - static_cast<std::int64_t>(minimum));
    const std::uint64_t threshold = (0 - span) % span;
    std::uint64_t value = 0;
    do value = Next(*State(handle)); while (value < threshold);
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(minimum) + static_cast<std::int64_t>(value % span));
}

extern "C" double absolute_random_real(std::uint64_t* handle) {
    if (!handle) return 0.0;
    return static_cast<double>(Next(*State(handle)) >> 11) * 0x1.0p-53;
}

extern "C" std::uint64_t absolute_random_entropy() {
    try {
        std::random_device source;
        std::uint64_t high = static_cast<std::uint64_t>(source()) << 32;
        return high ^ static_cast<std::uint64_t>(source());
    }
    catch (...) {
        // Native exceptions must never cross an Absolute extern "C" boundary.
        return 0;
    }
}

extern "C" std::int32_t absolute_random_fill(std::uint8_t* output, std::int32_t length) {
    if (length < 0 || (!output && length != 0)) return 0;
    try {
        std::random_device source;
        std::uniform_int_distribution<unsigned int> byte(0, 255);
        for (std::int32_t index = 0; index < length; ++index) {
            output[index] = static_cast<std::uint8_t>(byte(source));
        }
        return 1;
    }
    catch (...) {
        // Native exceptions must never cross an Absolute extern "C" boundary.
        return 0;
    }
}
