#if defined(_WIN32)
#define ABSOLUTE_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define ABSOLUTE_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

ABSOLUTE_TEST_EXPORT int absolute_load_test_value() {
    return 42;
}
