#include <cstdlib>

extern "C" int native_add(int left, int right) {
    return left + right;
}

extern "C" void native_increment(int* value) {
    if (value) ++*value;
}

extern "C" int* native_pointer_alias(int* value) {
    return value;
}

extern "C" void native_pointer_free(int* value) {
    std::free(value);
}
