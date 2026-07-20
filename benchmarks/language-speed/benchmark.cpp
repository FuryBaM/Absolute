#include <cstdint>
#include <iostream>

int main() {
    std::int32_t checksum = 0;

    for (std::int32_t round = 0; round < 5; ++round) {
        std::uint32_t state = static_cast<std::uint32_t>(123456789 + round);
        for (std::int32_t i = 0; i < 20000000; ++i) {
            state = state * 1664525u + 1013904223u;
            const auto signedState = static_cast<std::int32_t>(state);
            state ^= static_cast<std::uint32_t>(signedState >> 16);
        }
        checksum ^= static_cast<std::int32_t>(state);
    }

    std::cout << checksum << '\n';
}
