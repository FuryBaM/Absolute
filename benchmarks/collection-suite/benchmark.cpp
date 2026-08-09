using int32 = int;
using uint32 = unsigned int;
using int64 = long long;

#include <vector>
#include <unordered_map>

extern "C" int printf(const char*, ...);

bool equals(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

// Benchmark 1: push 1M elements, sum all, pop 500K, return sum + remaining count.
int64 vectorPushSum() {
    constexpr int32 N = 1000000;
    std::vector<int32> v;
    v.reserve(N);
    for (int32 i = 0; i < N; ++i) v.push_back(i * 17 + 3);
    int64 checksum = 0;
    for (int32 i = 0; i < N; ++i) checksum += v[i];
    for (int32 i = 0; i < 500000; ++i) v.pop_back();
    checksum += static_cast<int64>(v.size());
    return checksum;
}

// Benchmark 2: push 100K pseudorandom int32, insertion-sort, checksum.
int64 vectorSort() {
    constexpr int32 SIZE = 100000;
    std::vector<int32> values;
    values.reserve(SIZE);
    uint32 state = 123456789u;
    for (int32 i = 0; i < SIZE; ++i) {
        state = state * 1664525u + 1013904223u;
        values.push_back(static_cast<int32>(state));
    }
    for (int32 i = 1; i < SIZE; ++i) {
        int32 key = values[i];
        int32 j = i - 1;
        while (j >= 0 && values[j] > key) { values[j + 1] = values[j]; --j; }
        values[j + 1] = key;
    }
    int64 checksum = 0;
    for (int32 i = 0; i < SIZE; ++i) checksum += values[i];
    checksum += static_cast<int64>(values[0]) * 31;
    checksum += static_cast<int64>(values[SIZE - 1]) * 17;
    return checksum;
}

// Benchmark 3: insert 200K entries into unordered_map, lookup all, sum values.
int64 hashmapInsertLookup() {
    constexpr int32 N = 200000;
    std::unordered_map<int32, int32> map;
    map.reserve(N * 2);
    for (int32 i = 0; i < N; ++i) {
        int32 k = i * 17 + 31;
        int32 v = i * 13 + 7;
        map[k] = v;
    }
    int64 checksum = 0;
    for (int32 i = 0; i < N; ++i) {
        int32 k = i * 17 + 31;
        auto it = map.find(k);
        if (it != map.end()) checksum += it->second;
    }
    return checksum;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (equals(argv[1], "vector-push-sum")) printf("%lld\n", vectorPushSum());
    else if (equals(argv[1], "vector-sort")) printf("%lld\n", vectorSort());
    else if (equals(argv[1], "hashmap-insert-lookup")) printf("%lld\n", hashmapInsertLookup());
    else return 2;
    return 0;
}
