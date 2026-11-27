#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "cordis/foreign_task.h"
#include "cordis/service.h"
#include "cordis/tcp_read.h"
#include "cordis/tcp_stream_host.h"

namespace {
using namespace klang;

#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
static void close_socket(Socket fd) { closesocket(fd); }
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket invalid_socket = -1;
static void close_socket(Socket fd) { close(fd); }
#endif

struct TcpContract {
    using Api = k_cordis_tcp_read_api;
    static constexpr auto name = "TCP receive";
    static constexpr auto entry = "k_cordis_tcp_read_v1";
    static constexpr uint32_t version = 1;
    static bool Supports(const Api &api) { return api.receive != nullptr; }
};

using Service = cordis::Service<TcpContract>;
Service host;
weak_ptr<const Service::Generation> retired;
cordis::TcpStreamService stream_host;
weak_ptr<const cordis::TcpStreamService::Generation> stream_retired;
Socket peer = invalid_socket;
Socket receiver = invalid_socket;
atomic<int> polls = 0;
int cancellations, drains, finishes, closes;
std::thread peer_worker;
bool gate, finishing_started;
k_task_state terminal;

void require(bool ok, const char *message) {
    if (ok) return;
    fprintf(stderr, "%s\n", message);
    abort();
}

bool socket_open(Socket socket) {
    int type;
    SocketLength size = sizeof type;
    return getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &size) == 0;
}

struct Observed {
    const k_foreign_task *task;
    k_foreign_task_ready descriptor;

    explicit Observed(const k_foreign_task *task) : task(task), descriptor {
        {
            K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), task->result_size, this,
            [](void *frame, void *result) {
                auto self = static_cast<Observed *>(frame);
                ++polls;
                return self->task->poll(self->task->frame, result);
            },
            [](void *frame) {
                auto self = static_cast<Observed *>(frame);
                ++cancellations;
                self->task->request_cancel(self->task->frame);
            },
            [](void *frame) {
                auto self = static_cast<Observed *>(frame);
                ++drains;
                return !gate && self->task->drain(self->task->frame);
            },
            [](void *frame, k_task_state state, bool started) {
                unique_ptr<Observed> self(static_cast<Observed *>(frame));
                require(socket_open(receiver), "the provider closed the socket before finish");
                self->task->finish(self->task->frame, state, started);
                require(!socket_open(receiver), "finish left the native socket open");
                ++closes;
                ++finishes;
                receiver = invalid_socket;
                terminal = state;
                finishing_started = started;
            },
        },
        [](void *frame, const k_task_waker *waker) {
            auto op = static_cast<Observed *>(frame)->task;
            return !gate && k_foreign_task_arm(op, waker);
        },
    } {}
};

void initialize() {
    if (host.Pin()) return;
    auto path = getenv("K_CORDIS_TCP_READ_PROVIDER");
    require(path && *path, "missing TCP receive provider path");
    host.RegisterStatic("linked", k_cordis_tcp_read_v1);
    host.RegisterLibrary("shared", path);
    host.Select("shared");
}

void initialize_stream() {
    if (stream_host.Pin()) return;
    auto path = getenv("K_CORDIS_TCP_READ_PROVIDER");
    require(path && *path, "missing TCP stream provider path");
    stream_host.RegisterStatic("linked", k_cordis_tcp_stream_v1);
    stream_host.RegisterLibrary("shared", path);
    stream_host.Select("shared");
}

pair<Socket, Socket> connected_pair() {
    auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(listener != invalid_socket, "cannot create loopback listener");
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(listener, (const sockaddr *)&address, sizeof address) == 0 && listen(listener, 1) == 0,
            "cannot bind the loopback listener");
    SocketLength size = sizeof address;
    require(getsockname(listener, (sockaddr *)&address, &size) == 0, "cannot get loopback listener port");
    auto send_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(send_socket != invalid_socket && connect(send_socket, (const sockaddr *)&address, sizeof address) == 0,
            "cannot connect the loopback peer");
    auto read_socket = accept(listener, nullptr, nullptr);
    close_socket(listener);
    require(read_socket != invalid_socket, "cannot accept the loopback connection");
    return { send_socket, read_socket };
}

void loopback_pair() {
    if (peer != invalid_socket) close_socket(peer);
    auto [send_socket, read_socket] = connected_pair();
    peer = send_socket;
    receiver = read_socket;
}

struct ApplicationPeer {
    Socket sending;
    Socket receiving;
    bool transferred = false;

    ~ApplicationPeer() {
        close_socket(sending);
        if (!transferred) close_socket(receiving);
    }
};

}  // namespace

#ifdef _WIN32
#define TEST_API extern "C" __declspec(dllexport)
#else
#define TEST_API extern "C"
#endif

struct Handle { uint64_t token; };
struct Buffer { uint8_t *data; int64_t len; };

TEST_API Handle k_test_app_pair() {
    auto [sending, receiving] = connected_pair();
    auto pair = new ApplicationPeer { sending, receiving };
    return { (uint64_t)(uintptr_t)pair };
}

static ApplicationPeer &app_peer(Handle token) {
    return *reinterpret_cast<ApplicationPeer *>((uintptr_t)token.token);
}

TEST_API Handle k_test_app_socket(Handle token) {
    auto &p = app_peer(token);
    require(!p.transferred, "the application already owns the read socket");
    p.transferred = true;
    return { (uint64_t)p.receiving };
}

TEST_API int32_t k_test_app_socket_open(Handle token) {
    return socket_open(app_peer(token).receiving);
}

TEST_API void k_test_app_send(Handle token, int32_t value) {
    char byte = (char)value;
    require(send(app_peer(token).sending, &byte, 1, 0) == 1, "cannot send application payload");
}

TEST_API void k_test_app_end(Handle token) {
#ifdef _WIN32
    require(shutdown(app_peer(token).sending, SD_SEND) == 0, "cannot end the application payload");
#else
    require(shutdown(app_peer(token).sending, SHUT_WR) == 0, "cannot end the application payload");
#endif
}

TEST_API void k_test_app_close(Handle token) { delete &app_peer(token); }

TEST_API Handle k_test_receive() {
    try {
        initialize();
        require(receiver == invalid_socket, "the test already has a live receive");
        loopback_pair();
        auto provider = host.Pin();
        auto task = provider->Call<&TcpContract::Api::receive>((uint64_t)receiver);
        require(task != nullptr, "the provider rejected the loopback socket");
        auto owned = cordis::PinnedForeignTask<Service::Generation>::Bind(provider, task);
        require(owned != nullptr, "cannot pin the TCP receive provider");
        return { (uint64_t)(uintptr_t)&(new Observed(owned))->descriptor.base };
    } catch (const CompileError &e) {
        require(false, e.msg.c_str());
        return {};
    }
}

TEST_API void k_test_send(int32_t value) {
    auto byte = static_cast<char>(value);
    require(send(peer, &byte, 1, 0) == 1, "cannot send loopback data");
}

TEST_API void k_test_end_peer() {
#ifdef _WIN32
    require(shutdown(peer, SD_SEND) == 0, "cannot end the loopback peer's send side");
#else
    require(shutdown(peer, SHUT_WR) == 0, "cannot end the loopback peer's send side");
#endif
}

TEST_API int32_t k_test_poll_count() { return polls.load(); }

TEST_API void k_test_wake_idle_peer(int32_t byte, int32_t end) {
    require(!peer_worker.joinable(), "the test already has a peer worker");
    auto before = polls.load();
    require(before > 0, "the task has not been polled");
    peer_worker = std::thread([before, byte, end] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        require(polls.load() == before, "idle socket kept polling its parked task");
        k_test_send(byte);
        if (end) k_test_end_peer();
    });
}

TEST_API void k_test_reset_peer() {
    linger reset { 1, 0 };
    require(setsockopt(peer, SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&reset), sizeof reset) == 0,
            "cannot set abortive TCP close");
    close_socket(peer);
    peer = invalid_socket;
}

TEST_API void k_test_replace_provider() {
    retired = host.Pin();
    host.Select("linked");
    require(!retired.expired(), "replacement unloaded an active receive");
    gate = true;
}

TEST_API void k_test_accept_completion() {
    require(gate && cancellations == 1 && drains > 0, "cancellation did not wait for completion delivery");
    require(socket_open(receiver) && !retired.expired(), "the socket or old generation was reclaimed during drain");
    gate = false;
}

TEST_API void k_test_stream_validate_capacity() {
    initialize_stream();
    loopback_pair();
    auto provider = stream_host.Pin();
    for (uint32_t capacity : { 0u, K_TCP_STREAM_MAX_CAPACITY + 1u }) {
        auto invalid = cordis::TcpStreamReader::Open(provider, (uint64_t)receiver, capacity);
        require(!invalid && socket_open(receiver), "invalid capacity transferred the socket");
    }
    auto reader = cordis::TcpStreamReader::Open(provider, (uint64_t)receiver, 1);
    require(reader != nullptr, "cannot open a stream without starting a pump");
    reader.reset();
    require(!socket_open(receiver), "releasing an unclaimed reader left the socket open");
    close_socket(peer);
    peer = receiver = invalid_socket;
}

TEST_API Handle k_test_stream_open(uint32_t capacity) {
    try {
        initialize_stream();
        require(receiver == invalid_socket, "the test already has a live stream");
        loopback_pair();
        auto reader = cordis::TcpStreamReader::Open(stream_host.Pin(), (uint64_t)receiver, capacity);
        require(reader != nullptr, "the provider rejected the stream");
        return { (uint64_t)(uintptr_t)reader.release() };
    } catch (const CompileError &e) {
        require(false, e.msg.c_str());
        return {};
    }
}

static cordis::TcpStreamReader *stream_reader(Handle reader) {
    return reinterpret_cast<cordis::TcpStreamReader *>((uintptr_t)reader.token);
}

TEST_API Handle k_test_stream_pump(Handle reader) {
    auto task = stream_reader(reader)->Pump();
    require(task && !stream_reader(reader)->Pump(), "a stream must accept exactly one pump");
    return { (uint64_t)(uintptr_t)&(new Observed(task))->descriptor.base };
}

TEST_API k_tcp_stream_read_result k_test_stream_take(Handle reader, Buffer destination) {
    return stream_reader(reader)->Take({ destination.data, (size_t)destination.len });
}

TEST_API k_tcp_stream_stats k_test_stream_stats(Handle reader) { return stream_reader(reader)->Stats(); }

TEST_API void k_test_stream_close(Handle reader) { delete stream_reader(reader); }

TEST_API void k_test_stream_send_pattern(int32_t count) {
    unsigned char bytes[1024];
    require(count > 0 && count <= (int32_t)sizeof bytes, "invalid payload size");
    for (int32_t i = 0; i < count; ++i) bytes[i] = (unsigned char)(i % 251);
    for (int32_t sent = 0; sent < count;) {
        auto wrote = send(peer, reinterpret_cast<const char *>(bytes + sent), count - sent, 0);
        require(wrote > 0, "cannot send a loopback payload");
        sent += (int32_t)wrote;
    }
}

TEST_API void k_test_stream_replace() {
    stream_retired = stream_host.Pin();
    stream_host.Select("linked");
    require(!stream_retired.expired(), "replacement unloaded an active stream");
    gate = true;
}

TEST_API void k_test_stream_accept_completion() {
    require(gate && cancellations == 1 && drains > 0, "stream cancellation skipped drain");
    require(socket_open(receiver) && !stream_retired.expired(),
            "drain reclaimed the stream's socket or generation");
    gate = false;
}

TEST_API void k_test_stream_pin(int32_t live) {
    require(stream_retired.expired() == (live == 0), "unexpected lifetime of the retired stream provider");
}

TEST_API void k_test_check(int32_t expected, int32_t started, int32_t cancelled, int32_t unloaded) {
    if (peer_worker.joinable()) peer_worker.join();
    require(finishes == 1 && closes == 1 && receiver == invalid_socket, "receive was not closed exactly once");
    require(int32_t(terminal) == expected && finishing_started == (started != 0), "unexpected terminal status");
    require((polls > 0) == (started != 0) && cancellations == cancelled, "unexpected poll or cancel count");
    if (unloaded) require(retired.expired(), "the retired generation stayed live after finish");
    polls = cancellations = drains = finishes = closes = 0;
    if (peer != invalid_socket) close_socket(peer);
    peer = invalid_socket;
}
