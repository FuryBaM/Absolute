#include <iostream>

extern "C" int absolute_native_add(int left, int right);

int main() {
    const int result = absolute_native_add(20, 22);
    std::cout << "native-export=" << result << '\n';
    return result == 42 ? 0 : 1;
}
