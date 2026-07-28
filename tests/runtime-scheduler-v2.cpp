#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

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
    // accept/receive run first and block in the I/O executor. The sole
    // scheduler worker must remain available to start the client task.
    void* listener = absolute_net_tcp_listen("127.0.0.1", 0, 4);
    require(listener != nullptr);
    const std::int32_t port = absolute_net_tcp_port(listener);
    require(port > 0);
    auto* server = new NetworkContext{listener, port, false};
    auto* client = new NetworkContext{nullptr, port, false};
    void* serverTask = absolute_task_spawn_config(
        networkServer, server, -1, 3, "io-server");
    void* clientTask = absolute_task_spawn_config(
        networkClient, client, -1, -3, "io-client");
    server = await<NetworkContext>(serverTask);
    client = await<NetworkContext>(clientTask);
    require(server->ok);
    require(client->ok);
    delete server;
    delete client;
    absolute_net_tcp_close(listener);

    std::cout << "runtime-scheduler-v2=ok\n";
    return 0;
}
