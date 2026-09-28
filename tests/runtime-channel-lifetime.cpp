#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

extern "C" {
void* absolute_channel_create(std::int32_t capacity);
bool absolute_channel_send(void* channel, std::int64_t value);
bool absolute_channel_try_send(void* channel, std::int64_t value);
bool absolute_channel_receive_checked(void* channel, std::int64_t* value);
bool absolute_channel_try_receive(void* channel, std::int64_t* value);
bool absolute_channel_is_closed(void* channel);
void absolute_channel_destroy(void* channel);

void* absolute_transfer_channel_create(std::int32_t capacity);
void* absolute_transfer_channel_receive(void* channel);
void* absolute_transfer_channel_try_receive(void* channel);
bool absolute_transfer_channel_is_closed(void* channel);
void absolute_transfer_channel_destroy(void* channel);
}

namespace {
    void Require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    }

    void WaitUntilStarted(const std::atomic<bool>& started) {
        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();
        // Give the worker a chance to enter the blocking operation. Correctness
        // does not depend on this delay: if destroy wins earlier, registry
        // resolution must reject the stale handle instead.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int main() {
    constexpr int rounds = 100;

    for (int round = 0; round < rounds; ++round) {
        {
            void* channel = absolute_channel_create(1);
            std::atomic<bool> started{false};
            bool received = true;
            std::thread receiver([&] {
                std::int64_t value = 0;
                started.store(true, std::memory_order_release);
                received = absolute_channel_receive_checked(channel, &value);
            });
            WaitUntilStarted(started);
            absolute_channel_destroy(channel);
            receiver.join();

            Require(!received, "destroy did not wake a blocked channel receiver");
            Require(absolute_channel_is_closed(channel),
                "destroyed channel handle did not become closed/invalid");
            std::int64_t staleValue = 0;
            Require(!absolute_channel_try_receive(channel, &staleValue),
                "stale channel handle received after destroy");
            Require(!absolute_channel_try_send(channel, 1),
                "stale channel handle sent after destroy");
        }

        {
            void* channel = absolute_channel_create(1);
            Require(absolute_channel_send(channel, 1),
                "failed to seed bounded channel");
            std::atomic<bool> started{false};
            bool sent = true;
            std::thread sender([&] {
                started.store(true, std::memory_order_release);
                sent = absolute_channel_send(channel, 2);
            });
            WaitUntilStarted(started);
            absolute_channel_destroy(channel);
            sender.join();
            Require(!sent, "destroy did not wake a blocked channel sender");
        }

        {
            void* channel = absolute_transfer_channel_create(1);
            std::atomic<bool> started{false};
            void* received = reinterpret_cast<void*>(1);
            std::thread receiver([&] {
                started.store(true, std::memory_order_release);
                received = absolute_transfer_channel_receive(channel);
            });
            WaitUntilStarted(started);
            absolute_transfer_channel_destroy(channel);
            receiver.join();

            Require(received == nullptr,
                "destroy did not wake a blocked transfer-channel receiver");
            Require(absolute_transfer_channel_is_closed(channel),
                "destroyed transfer-channel handle did not become closed/invalid");
            Require(absolute_transfer_channel_try_receive(channel) == nullptr,
                "stale transfer-channel handle received after destroy");
        }
    }

    std::cout << "runtime-channel-lifetime=ok\n";
    return 0;
}
