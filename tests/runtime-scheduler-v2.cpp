#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <sys/socket.h>
#include <unistd.h>

#include "socket_reactor.h"
#endif

extern "C" {
void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, std::int32_t core,
    std::int32_t priority, const char* role);
void* absolute_task_await(void* handle);
void absolute_task_delay(std::int32_t milliseconds);
std::int32_t absolute_scheduler_worker_count();

void* absolute_channel_create(std::int32_t capacity);
bool absolute_channel_send(void* channel, std::int64_t value);
std::int64_t absolute_channel_receive(void* channel);
std::int32_t absolute_channel_count(void* channel);
void absolute_channel_close(void* channel);
void absolute_channel_destroy(void* channel);

void* absolute_net_tcp_connect(const char* host, std::int32_t port);
void* absolute_net_tcp_listen(
    const char* host, std::int32_t port, std::int32_t backlog);
void* absolute_net_tcp_accept(void* handle);
std::int32_t absolute_net_tcp_send(void* handle, const char* text);
const char* absolute_net_tcp_receive(void* handle, std::int32_t maximumBytes);
std::int32_t absolute_net_tcp_port(void* handle);
void absolute_net_tcp_close(void* handle);
}

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}

struct ChannelContext {
    void* channel = nullptr;
    std::int32_t count = 0;
    std::int64_t checksum = 0;
};

void producer(void* opaque) {
    auto* context = static_cast<ChannelContext*>(opaque);
    for (std::int32_t value = 1; value <= context->count; ++value) {
        if (!absolute_channel_send(context->channel, value)) std::abort();
        context->checksum += value;
    }
}

void consumer(void* opaque) {
    auto* context = static_cast<ChannelContext*>(opaque);
    for (std::int32_t index = 0; index < context->count; ++index)
        context->checksum += absolute_channel_receive(context->channel);
}

struct NestedContext {
    std::int32_t value = 0;
};

void nestedChild(void* opaque) {
    static_cast<NestedContext*>(opaque)->value = 42;
}

void nestedParent(void* opaque) {
    auto* parent = static_cast<NestedContext*>(opaque);
    auto* child = new NestedContext;
    void* task = absolute_task_spawn_config(
        nestedChild, child, -1, 0, "nested-child");
    auto* completed = static_cast<NestedContext*>(absolute_task_await(task));
    parent->value = completed->value + 1;
    delete completed;
}

struct DelayContext {
    std::int32_t value = 0;
    bool delay = false;
};

void delayedTask(void* opaque) {
    auto* context = static_cast<DelayContext*>(opaque);
    if (context->delay) absolute_task_delay(20);
    context->value = context->delay ? 1 : 2;
}

struct NetworkContext {
    void* listener = nullptr;
    std::int32_t port = 0;
    bool ok = false;
};

void networkServer(void* opaque) {
    auto* context = static_cast<NetworkContext*>(opaque);
    void* socket = absolute_net_tcp_accept(context->listener);
    if (!socket) return;
    const char* request = absolute_net_tcp_receive(socket, 16);
    context->ok = request && std::strcmp(request, "ping") == 0 &&
        absolute_net_tcp_send(socket, "pong") == 4;
    std::free(const_cast<char*>(request));
    absolute_net_tcp_close(socket);
}

void networkClient(void* opaque) {
    auto* context = static_cast<NetworkContext*>(opaque);
    void* socket = absolute_net_tcp_connect("127.0.0.1", context->port);
    if (!socket) return;
    if (absolute_net_tcp_send(socket, "ping") != 4) {
        absolute_net_tcp_close(socket);
        return;
    }
    const char* response = absolute_net_tcp_receive(socket, 16);
    context->ok = response && std::strcmp(response, "pong") == 0;
    std::free(const_cast<char*>(response));
    absolute_net_tcp_close(socket);
}

#if defined(__linux__)
struct ReactorWaitContext {
    int descriptor = -1;
    bool ready = false;
};

void reactorReadWait(void* opaque) {
    auto* context = static_cast<ReactorWaitContext*>(opaque);
    context->ready = Absolute::RuntimeDetail::WaitSocketReady(
        context->descriptor, Absolute::RuntimeDetail::SocketReadyRead);
}

struct ReactorSignalContext {
    int descriptor = -1;
    bool sent = false;
};

void reactorSignal(void* opaque) {
    auto* context = static_cast<ReactorSignalContext*>(opaque);
    absolute_task_delay(10);
    const char bytes[2] = {'a', 'b'};
    context->sent = write(context->descriptor, bytes, sizeof(bytes)) ==
        static_cast<ssize_t>(sizeof(bytes));
}
#endif

template <class T>
T* await(void* task) {
    return static_cast<T*>(absolute_task_await(task));
}
}  // namespace

int main() {
#if defined(_WIN32)
    _putenv_s("ABSOLUTE_SCHEDULER_WORKERS", "1");
    _putenv_s("ABSOLUTE_IO_WORKERS", "2");
#else
    setenv("ABSOLUTE_SCHEDULER_WORKERS", "1", 1);
    setenv("ABSOLUTE_IO_WORKERS", "2", 1);
#endif

    if (absolute_scheduler_worker_count() != 1) std::abort();

    std::cerr << "phase=channel\n";
    // The high-priority producer fills the capacity-one queue first. With the
    // old blocking scheduler this deadlocked forever because no worker remained
    // to run the consumer.
    void* channel = absolute_channel_create(1);
    auto* producerContext = new ChannelContext{channel, 2'000, 0};
    auto* consumerContext = new ChannelContext{channel, 2'000, 0};
    void* producerTask = absolute_task_spawn_config(
        producer, producerContext, -1, 3, "producer");
    void* consumerTask = absolute_task_spawn_config(
        consumer, consumerContext, -1, -3, "consumer");

    producerContext = await<ChannelContext>(producerTask);
    consumerContext = await<ChannelContext>(consumerTask);
    require(producerContext->checksum == consumerContext->checksum);
    require(absolute_channel_count(channel) == 0);
    delete producerContext;
    delete consumerContext;
    absolute_channel_close(channel);
    absolute_channel_destroy(channel);

    std::cerr << "phase=nested-await\n";
    // Await from inside a worker must run the child through the same scheduler
    // slot instead of blocking the only OS worker.
    auto* nested = new NestedContext;
    void* parentTask = absolute_task_spawn_config(
        nestedParent, nested, -1, 0, "nested-parent");
    nested = await<NestedContext>(parentTask);
    require(nested->value == 43);
    delete nested;

    std::cerr << "phase=delay\n";
    // A delayed task yields scheduler capacity while its deadline is pending.
    auto* slow = new DelayContext{0, true};
    auto* fast = new DelayContext{0, false};
    void* slowTask = absolute_task_spawn_config(
        delayedTask, slow, -1, 3, "timer");
    void* fastTask = absolute_task_spawn_config(
        delayedTask, fast, -1, -3, "ready");
    slow = await<DelayContext>(slowTask);
    fast = await<DelayContext>(fastTask);
    require(slow->value == 1);
    require(fast->value == 2);
    delete slow;
    delete fast;

    std::cerr << "phase=network-io\n";
    // On Linux, eight pending accepts exceed the two-thread fallback I/O pool.
    // They can all make progress only when socket readiness is handled by
    // epoll instead of occupying those threads. Other platforms retain the
    // portable one-pair offload contract until their native reactor lands.
#if defined(__linux__)
    constexpr std::int32_t networkPairs = 8;
#else
    constexpr std::int32_t networkPairs = 1;
#endif
    std::vector<void*> listeners;
    std::vector<std::int32_t> ports;
    std::vector<void*> serverTasks;
    std::vector<void*> clientTasks;
    listeners.reserve(networkPairs);
    ports.reserve(networkPairs);
    serverTasks.reserve(networkPairs);
    clientTasks.reserve(networkPairs);
    for (std::int32_t index = 0; index < networkPairs; ++index) {
        void* listener = absolute_net_tcp_listen("127.0.0.1", 0, 4);
        require(listener != nullptr);
        const std::int32_t port = absolute_net_tcp_port(listener);
        require(port > 0);
        listeners.push_back(listener);
        ports.push_back(port);
        serverTasks.push_back(absolute_task_spawn_config(
            networkServer,
            new NetworkContext{listener, port, false},
            -1, 3, "io-server"));
    }
    for (std::int32_t index = 0; index < networkPairs; ++index) {
        const std::int32_t port =
            ports[static_cast<std::size_t>(index)];
        clientTasks.push_back(absolute_task_spawn_config(
            networkClient,
            new NetworkContext{nullptr, port, false},
            -1, -3, "io-client"));
    }
    for (std::int32_t index = 0; index < networkPairs; ++index) {
        auto* server = await<NetworkContext>(
            serverTasks[static_cast<std::size_t>(index)]);
        auto* client = await<NetworkContext>(
            clientTasks[static_cast<std::size_t>(index)]);
        require(server->ok);
        require(client->ok);
        delete server;
        delete client;
        absolute_net_tcp_close(
            listeners[static_cast<std::size_t>(index)]);
    }

#if defined(__linux__)
    std::cerr << "phase=epoll-shared-descriptor\n";
    int socketPair[2] = {-1, -1};
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, socketPair) == 0);
    auto* firstWait = new ReactorWaitContext{socketPair[0], false};
    auto* secondWait = new ReactorWaitContext{socketPair[0], false};
    auto* signal = new ReactorSignalContext{socketPair[1], false};
    void* firstWaitTask = absolute_task_spawn_config(
        reactorReadWait, firstWait, -1, 3, "epoll-read-1");
    void* secondWaitTask = absolute_task_spawn_config(
        reactorReadWait, secondWait, -1, 3, "epoll-read-2");
    void* signalTask = absolute_task_spawn_config(
        reactorSignal, signal, -1, -3, "epoll-signal");
    firstWait = await<ReactorWaitContext>(firstWaitTask);
    secondWait = await<ReactorWaitContext>(secondWaitTask);
    signal = await<ReactorSignalContext>(signalTask);
    require(firstWait->ready);
    require(secondWait->ready);
    require(signal->sent);
    delete firstWait;
    delete secondWait;
    delete signal;
    close(socketPair[0]);
    close(socketPair[1]);
#endif

    std::cout << "runtime-scheduler-v2=ok\n";
    return 0;
}
