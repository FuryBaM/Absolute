#include <cstdint>
#include <cstdlib>

extern "C" void* absolute_test_handle_create(std::int32_t tag) {
    auto* value = static_cast<std::int32_t*>(std::malloc(sizeof(std::int32_t)));
    if (value) *value = tag;
    return value;
}

extern "C" std::int32_t absolute_test_handle_tag(void* handle) {
    return handle ? *static_cast<std::int32_t*>(handle) : -1;
}

extern "C" void absolute_test_handle_destroy(void* handle) {
    std::free(handle);
}
