extern "C" int native_add(int left, int right) {
    return left + right;
}

extern "C" void native_increment(int* value) {
    if (value) ++*value;
}
