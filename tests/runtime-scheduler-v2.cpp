#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
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
std::int32_t absolute_net_tcp_set_timeout(
    void* handle, std::int32_t milliseconds);
std::int32_t absolute_net_tcp_port(void* handle);
void absolute_net_tcp_close(void* handle);
void* absolute_net_udp_bind(const char* host, std::int32_t port);
std::int32_t absolute_net_udp_send_to(
    void* handle, const char* host, std::int32_t port, const char* text);
const char* absolute_net_udp_receive_from(
    void* handle, std::int32_t maximumBytes);
void absolute_net_udp_close(void* handle);
const char* absolute_net_error();
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
    if (!socket) {
        std::cerr << "network server accept failed: "
            << absolute_net_error() << '\n';
        return;
    }
    const char* request = absolute_net_tcp_receive(socket, 16);
    context->ok = request && std::strcmp(request, "ping") == 0 &&
        absolute_net_tcp_send(socket, "pong") == 4;
    if (!context->ok)
        std::cerr << "network server exchange failed: "
            << absolute_net_error() << '\n';
    std::free(const_cast<char*>(request));
    absolute_net_tcp_close(socket);
}

void networkClient(void* opaque) {
    auto* context = static_cast<NetworkContext*>(opaque);
    void* socket = absolute_net_tcp_connect("127.0.0.1", context->port);
    if (!socket) {
        std::cerr << "network client connect failed: "
            << absolute_net_error() << '\n';
        return;
    }
    if (absolute_net_tcp_send(socket, "ping") != 4) {
        std::cerr << "network client send failed: "
            << absolute_net_error() << '\n';
        absolute_net_tcp_close(socket);
        return;
    }
    const char* response = absolute_net_tcp_receive(socket, 16);
    context->ok = response && std::strcmp(response, "pong") == 0;
    if (!context->ok)
        std::cerr << "network client receive failed: "
            << absolute_net_error() << '\n';
    std::free(const_cast<char*>(response));
    absolute_net_tcp_close(socket);
}

struct UdpContext {
    void* socket = nullptr;
    std::int32_t port = 0;
    bool ok = false;
};

void udpServer(void* opaque) {
    auto* context = static_cast<UdpContext*>(opaque);
    const char* request =
        absolute_net_udp_receive_from(context->socket, 16);
    context->ok = request && std::strcmp(request, "datagram") == 0;
    if (!context->ok)
        std::cerr << "udp server receive failed: "
            << absolute_net_error() << '\n';
    std::free(const_cast<char*>(request));
}

void udpClient(void* opaque) {
    auto* context = static_cast<UdpContext*>(opaque);
    context->ok = absolute_net_udp_send_to(
        context->socket, "127.0.0.1",
        context->port, "datagram") == 8;
    if (!context->ok)
        std::cerr << "udp client send failed: "
            << absolute_net_error() << '\n';
}

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
struct SharedReceiveContext {
    void* socket = nullptr;
    char value = '\0';
    bool ok = false;
};

void sharedReceive(void* opaque) {
    auto* context = static_cast<SharedReceiveContext*>(opaque);
    const char* text = absolute_net_tcp_receive(context->socket, 1);
    context->ok = text && std::strlen(text) == 1;
    if (context->ok) context->value = text[0];
    if (!context->ok)
        std::cerr << "shared socket receive failed: "
            << absolute_net_error() << '\n';
    std::free(const_cast<char*>(text));
}

struct SharedSendContext {
    void* socket = nullptr;
    bool ok = false;
};

void sharedSend(void* opaque) {
    auto* context = static_cast<SharedSendContext*>(opaque);
    absolute_task_delay(10);
    context->ok = absolute_net_tcp_send(context->socket, "xy") == 2;
    if (!context->ok)
        std::cerr << "shared socket send failed: "
            << absolute_net_error() << '\n';
}

struct TimedReceiveContext {
    void* socket = nullptr;
    bool timedOut = false;
    std::int64_t elapsedMilliseconds = 0;
};

void timedReceive(void* opaque) {
    auto* context = static_cast<TimedReceiveContext*>(opaque);
    const auto started = std::chrono::steady_clock::now();
    const char* text = absolute_net_tcp_receive(context->socket, 1);
    context->elapsedMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    const char* error = absolute_net_error();
    context->timedOut = !text && error &&
        std::strstr(error, "timed out") != nullptr;
    std::free(const_cast<char*>(text));
}
#endif

#if defined(__linux__) || defined(__APPLE__)
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
    // Eight pending accepts exceed the two-thread fallback I/O pool. They can
    // all make progress only when Linux epoll or Windows IOCP owns the wait
    // instead of occupying those threads.
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
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

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    std::cerr << "phase=shared-socket-receive\n";
    void* sharedListener =
        absolute_net_tcp_listen("127.0.0.1", 0, 4);
    require(sharedListener != nullptr);
    const std::int32_t sharedPort =
        absolute_net_tcp_port(sharedListener);
    require(sharedPort > 0);
    void* sharedClient =
        absolute_net_tcp_connect("127.0.0.1", sharedPort);
    require(sharedClient != nullptr);
    void* sharedServer = absolute_net_tcp_accept(sharedListener);
    require(sharedServer != nullptr);
    require(absolute_net_tcp_set_timeout(sharedServer, 1'000) == 1);
    void* firstReceiveTask = absolute_task_spawn_config(
        sharedReceive,
        new SharedReceiveContext{sharedServer, '\0', false},
        -1, 3, "shared-receive-1");
    void* secondReceiveTask = absolute_task_spawn_config(
        sharedReceive,
        new SharedReceiveContext{sharedServer, '\0', false},
        -1, 3, "shared-receive-2");
    void* sharedSendTask = absolute_task_spawn_config(
        sharedSend, new SharedSendContext{sharedClient, false},
        -1, -3, "shared-send");
    auto* firstReceive =
        await<SharedReceiveContext>(firstReceiveTask);
    auto* secondReceive =
        await<SharedReceiveContext>(secondReceiveTask);
    auto* sharedSendResult =
        await<SharedSendContext>(sharedSendTask);
    require(firstReceive->ok);
    require(secondReceive->ok);
    require(sharedSendResult->ok);
    require(
        (firstReceive->value == 'x' &&
            secondReceive->value == 'y') ||
        (firstReceive->value == 'y' &&
            secondReceive->value == 'x'));
    delete firstReceive;
    delete secondReceive;
    delete sharedSendResult;
    absolute_net_tcp_close(sharedServer);
    absolute_net_tcp_close(sharedClient);
    absolute_net_tcp_close(sharedListener);

    std::cerr << "phase=native-socket-deadline\n";
    void* timeoutListener =
        absolute_net_tcp_listen("127.0.0.1", 0, 4);
    require(timeoutListener != nullptr);
    const std::int32_t timeoutPort =
        absolute_net_tcp_port(timeoutListener);
    require(timeoutPort > 0);
    void* timeoutClient =
        absolute_net_tcp_connect("127.0.0.1", timeoutPort);
    require(timeoutClient != nullptr);
    void* timeoutServer = absolute_net_tcp_accept(timeoutListener);
    require(timeoutServer != nullptr);
    require(absolute_net_tcp_set_timeout(timeoutServer, 40) == 1);
    void* timeoutTask = absolute_task_spawn_config(
        timedReceive,
        new TimedReceiveContext{timeoutServer, false, 0},
        -1, 3, "socket-deadline");
    auto* deadlineProgress = new DelayContext{0, false};
    void* deadlineProgressTask = absolute_task_spawn_config(
        delayedTask, deadlineProgress,
        -1, -3, "deadline-progress");
    deadlineProgress = await<DelayContext>(deadlineProgressTask);
    require(deadlineProgress->value == 2);
    delete deadlineProgress;
    auto* timeoutResult =
        await<TimedReceiveContext>(timeoutTask);
    require(timeoutResult->timedOut);
    require(timeoutResult->elapsedMilliseconds >= 10);
    require(timeoutResult->elapsedMilliseconds < 5'000);
    delete timeoutResult;
    absolute_net_tcp_close(timeoutServer);
    absolute_net_tcp_close(timeoutClient);
    absolute_net_tcp_close(timeoutListener);
#endif

    std::cerr << "phase=udp-io\n";
    void* udpListener = absolute_net_udp_bind("127.0.0.1", 0);
    void* udpSender = absolute_net_udp_bind("127.0.0.1", 0);
    require(udpListener != nullptr);
    require(udpSender != nullptr);
    const std::int32_t udpPort = absolute_net_tcp_port(udpListener);
    require(udpPort > 0);
    void* udpServerTask = absolute_task_spawn_config(
        udpServer, new UdpContext{udpListener, udpPort, false},
        -1, 3, "udp-server");
    void* udpClientTask = absolute_task_spawn_config(
        udpClient, new UdpContext{udpSender, udpPort, false},
        -1, -3, "udp-client");
    auto* udpServerResult = await<UdpContext>(udpServerTask);
    auto* udpClientResult = await<UdpContext>(udpClientTask);
    require(udpServerResult->ok);
    require(udpClientResult->ok);
    delete udpServerResult;
    delete udpClientResult;
    absolute_net_udp_close(udpListener);
    absolute_net_udp_close(udpSender);

#if defined(__linux__) || defined(__APPLE__)
    std::cerr << "phase=native-readiness-shared-descriptor\n";
    int socketPair[2] = {-1, -1};
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, socketPair) == 0);
    auto* firstWait = new ReactorWaitContext{socketPair[0], false};
    auto* secondWait = new ReactorWaitContext{socketPair[0], false};
    auto* signal = new ReactorSignalContext{socketPair[1], false};
    void* firstWaitTask = absolute_task_spawn_config(
        reactorReadWait, firstWait, -1, 3, "native-read-1");
    void* secondWaitTask = absolute_task_spawn_config(
        reactorReadWait, secondWait, -1, 3, "native-read-2");
    void* signalTask = absolute_task_spawn_config(
        reactorSignal, signal, -1, -3, "native-signal");
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
