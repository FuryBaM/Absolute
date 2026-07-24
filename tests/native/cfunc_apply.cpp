#include <cstdint>

extern "C" std::int32_t absolute_apply(std::int32_t (*transform)(std::int32_t), std::int32_t value) {
    return transform ? transform(value) : 0;
}
