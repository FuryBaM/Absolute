using int32 = int;
using uint32 = unsigned int;
using int64 = long long;

extern "C" int printf(const char*, ...);

bool equals(const char* left, const char* right) {
    while (*left != 0 && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

int32 mix() {
    int32 checksum = 0;
    for (int32 round = 0; round < 5; ++round) {
        uint32 state = static_cast<uint32>(123456789 + round);
        for (int32 i = 0; i < 20000000; ++i) {
            state = state * 1664525u + 1013904223u;
            state ^= static_cast<uint32>(static_cast<int32>(state) >> 16);
        }
        checksum ^= static_cast<int32>(state);
    }
    return checksum;
}

int32 primes() {
    int32 count = 1;
    for (int32 candidate = 3; candidate <= 2000000; candidate += 2) {
        bool isPrime = true;
        for (int32 divisor = 3; divisor * divisor <= candidate; divisor += 2) {
            if (candidate % divisor == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) ++count;
    }
    return count;
}

int64 collatz() {
    int64 totalSteps = 0;
    for (int32 seed = 1; seed <= 2000000; ++seed) {
        int64 value = seed;
        while (value > 1) {
            value = value % 2 == 0 ? value / 2 : value * 3 + 1;
            ++totalSteps;
        }
    }
    return totalSteps;
}

int64 gcd() {
    int64 checksum = 0;
    for (int32 i = 1; i <= 5000000; ++i) {
        int32 left = i * 17 + 12345;
        int32 right = i * 31 + 6789;
        while (right != 0) {
            const int32 remainder = left % right;
            left = right;
            right = remainder;
        }
        checksum += left;
    }
    return checksum;
}

int32 floatingPoint() {
    double value = 0.5;
    double sum = 0.0;
    for (int32 i = 0; i < 100000000; ++i) {
        value = value * 1.000000119 + 0.0000001;
        if (value > 2.0) value -= 1.5;
        sum += value;
    }
    return static_cast<int32>(sum);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (equals(argv[1], "mix")) printf("%d\n", mix());
    else if (equals(argv[1], "primes")) printf("%d\n", primes());
    else if (equals(argv[1], "collatz")) printf("%lld\n", collatz());
    else if (equals(argv[1], "gcd")) printf("%lld\n", gcd());
    else if (equals(argv[1], "floating")) printf("%d\n", floatingPoint());
    else return 2;
    return 0;
}
