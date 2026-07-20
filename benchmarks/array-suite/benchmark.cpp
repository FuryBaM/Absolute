using int32 = int;
using uint32 = unsigned int;
using int64 = long long;

extern "C" int printf(const char*, ...);

bool equals(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

int64 scan() {
    constexpr int32 size = 1000000;
    uint32 values[size];
    for (int32 index = 0; index < size; ++index) values[index] = index * 17u + 3u;
    int64 checksum = 0;
    for (int32 round = 0; round < 100; ++round) {
        for (int32 index = 0; index < size; ++index) {
            values[index] = values[index] * 3u + static_cast<uint32>(round);
            checksum += static_cast<int32>(values[index]);
        }
    }
    return checksum;
}

int64 randomAccess() {
    constexpr int32 size = 1048576;
    constexpr uint32 mask = size - 1;
    int32 values[size];
    for (int32 index = 0; index < size; ++index) values[index] = index * 31 + 7;
    uint32 state = 123456789u;
    int64 checksum = 0;
    for (int32 iteration = 0; iteration < 100000000; ++iteration) {
        state = state * 1664525u + 1013904223u;
        const uint32 index = state & mask;
        values[index] = static_cast<int32>(static_cast<uint32>(values[index]) ^ state);
        checksum += values[index];
    }
    return checksum;
}

int64 insertionSort() {
    constexpr int32 size = 30000;
    int32 values[size];
    uint32 state = 123456789u;
    for (int32 index = 0; index < size; ++index) {
        state = state * 1664525u + 1013904223u;
        values[index] = static_cast<int32>(state);
    }
    for (int32 index = 1; index < size; ++index) {
        const int32 key = values[index];
        int32 position = index - 1;
        while (position >= 0) {
            if (values[position] <= key) break;
            values[position + 1] = values[position];
            --position;
        }
        values[position + 1] = key;
    }
    int64 checksum = 0;
    for (int32 index = 0; index < size; ++index) checksum += values[index];
    checksum += static_cast<int64>(values[0]) * 31;
    checksum += static_cast<int64>(values[size - 1]) * 17;
    return checksum;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (equals(argv[1], "scan")) printf("%lld\n", scan());
    else if (equals(argv[1], "random-access")) printf("%lld\n", randomAccess());
    else if (equals(argv[1], "insertion-sort")) printf("%lld\n", insertionSort());
    else return 2;
    return 0;
}
