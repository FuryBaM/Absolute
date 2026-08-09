#include <cstdint>
#include <cstdlib>

// Live handles are counted so the Absolute side can assert that destroy() ran,
// rather than describing it in a comment. Without a count, a wrapper whose
// destroy() is never called still passes every observation made before cleanup.
static std::int32_t absolute_test_handle_live_count = 0;

extern "C" void* absolute_test_handle_create(std::int32_t tag) {
    auto* value = static_cast<std::int32_t*>(std::malloc(sizeof(std::int32_t)));
    if (value) *value = tag;
    if (value) ++absolute_test_handle_live_count;
    return value;
}

extern "C" std::int32_t absolute_test_handle_live() {
    return absolute_test_handle_live_count;
}

extern "C" std::int32_t absolute_test_handle_tag(void* handle) {
    return handle ? *static_cast<std::int32_t*>(handle) : -1;
}

extern "C" void absolute_test_handle_destroy(void* handle) {
    if (handle) --absolute_test_handle_live_count;
    std::free(handle);
}
