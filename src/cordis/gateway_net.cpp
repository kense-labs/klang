#include "tcp_socket.h"
#include "gateway_net.h"

#ifndef _WIN32
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {
namespace tcp = klang::cordis::tcp;
using Clock = std::chrono::steady_clock;
using Millis = std::chrono::milliseconds;

struct Socket {
    tcp::Socket fd = tcp::invalid_socket;
    ~Socket() { if (fd != tcp::invalid_socket) tcp::Close(fd); }
};

struct Network {
    void *state = nullptr;
    ~Network() { tcp::Stop(state); }
};

struct Connection {
    std::shared_ptr<Network> network;
    Clock::time_point deadline;
    Socket socket;
};

constexpr int accept_tick_ms = 25;

bool Nonblocking(tcp::Socket fd) {
#ifdef _WIN32
    unsigned long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool WouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

bool RetryAccept(int error) {
    if (WouldBlock(error) || error == tcp::interrupted_error) return true;
#ifdef _WIN32
    return error == WSAECONNRESET || error == WSAECONNABORTED;
#else
    return error == ECONNABORTED;
#endif
}

int Send(tcp::Socket fd, const void *bytes, int size) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return static_cast<int>(send(fd, static_cast<const char *>(bytes), size, flags));
}

int Poll(tcp::Socket fd, short interest, int millis) {
#ifdef _WIN32
    WSAPOLLFD event { fd, interest, 0 };
    return WSAPoll(&event, 1, millis);
#else
    pollfd event { fd, interest, 0 };
    return poll(&event, 1, millis);
#endif
}

// Admission/expiry reports are best effort. The acceptor never waits for
// an unadmitted or expired client to read its terminal frame.
void SendControl(tcp::Socket fd, uint8_t tag) {
    const uint8_t frame[5] { tag, 0, 0, 0, 0 };
    Send(fd, frame, sizeof frame);
}

int32_t Transfer(const Connection &connection, uint8_t *out, const uint8_t *in, int64_t size) {
    if (size < 0 || (size && !in && !out)) return K_GATEWAY_CLOSED;
    auto fd = connection.socket.fd;
    for (int64_t offset = 0; offset < size;) {
        const auto left = connection.deadline - Clock::now();
        if (left <= Clock::duration::zero())
            return in && offset ? K_GATEWAY_CLOSED : K_GATEWAY_DEADLINE;
        const auto chunk = static_cast<int>(std::min<int64_t>(size - offset, INT32_MAX));
        int n = in ? Send(fd, in + offset, chunk)
                   : static_cast<int>(recv(fd, reinterpret_cast<char *>(out + offset), chunk, 0));
        if (n > 0) {
            offset += n;
            continue;
        }
        if (!n) return in || !offset ? K_GATEWAY_CLOSED : K_GATEWAY_BAD_REQUEST;
        int error = tcp::SocketError();
        if (error == tcp::interrupted_error) continue;
        if (!WouldBlock(error)) return K_GATEWAY_CLOSED;
        const auto wait = std::chrono::ceil<Millis>(connection.deadline - Clock::now()).count();
        if (wait <= 0) continue;
        int ready = Poll(fd, in ? POLLOUT : POLLIN, static_cast<int>(wait));
        if (ready < 0 && tcp::SocketError() != tcp::interrupted_error) return K_GATEWAY_CLOSED;
    }
    return K_GATEWAY_OK;
}

struct Listener {
    std::shared_ptr<Network> network;
    Socket socket;
    uint32_t port = 0, capacity = 0, peak_pending = 0;
    Millis budget;
    std::mutex mutex;
    std::condition_variable available;
    std::deque<std::unique_ptr<Connection>> pending;
    std::atomic<bool> closing { false };
    bool stopped = false;
    std::thread acceptor;

    ~Listener() {
        closing = true;
        if (acceptor.joinable()) acceptor.join();
    }

    void ExpirePending(Clock::time_point now) {
        while (!pending.empty() && pending.front()->deadline <= now) {
            SendControl(pending.front()->socket.fd, K_GATEWAY_DEADLINE);
            pending.pop_front();
        }
    }

    void Run() {
        while (!closing) {
            int wait = accept_tick_ms;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto now = Clock::now();
                ExpirePending(now);
                if (!pending.empty())
                    wait = std::min(wait, static_cast<int>(std::chrono::ceil<Millis>(pending.front()->deadline - now).count()));
            }
            int ready = Poll(socket.fd, POLLIN, wait);
            if (!ready) continue;
            if (ready < 0) {
                if (tcp::SocketError() == tcp::interrupted_error) continue;
                break;
            }
            Socket incoming;
            incoming.fd = accept(socket.fd, nullptr, nullptr);
            if (incoming.fd == tcp::invalid_socket) {
                int error = tcp::SocketError();
                if (RetryAccept(error)) continue;
                break;
            }
            auto deadline = Clock::now() + budget;
            if (!Nonblocking(incoming.fd)) continue;
            int enabled = 1;
#ifdef SO_NOSIGPIPE
            if (setsockopt(incoming.fd, SOL_SOCKET, SO_NOSIGPIPE,
                           reinterpret_cast<const char *>(&enabled), sizeof enabled) != 0) continue;
#endif
            if (setsockopt(incoming.fd, IPPROTO_TCP, TCP_NODELAY,
                           reinterpret_cast<const char *>(&enabled), sizeof enabled) != 0) continue;
            std::lock_guard<std::mutex> lock(mutex);
            ExpirePending(Clock::now());
            if (pending.size() >= capacity) {
                SendControl(incoming.fd, K_GATEWAY_BUSY);
                continue;
            }
            auto &slot = pending.emplace_back(std::make_unique<Connection>());
            slot->network = network;
            slot->deadline = deadline;
            std::swap(slot->socket.fd, incoming.fd);
            peak_pending = std::max(peak_pending, static_cast<uint32_t>(pending.size()));
            available.notify_one();
        }
    }

    void RunAndNotify() noexcept {
        try { Run(); } catch (...) {}
        std::lock_guard<std::mutex> lock(mutex);
        stopped = true;
        available.notify_one();
    }

    Connection *Next() {
        std::unique_lock<std::mutex> lock(mutex);
        available.wait(lock, [&] { return stopped || !pending.empty(); });
        if (pending.empty()) return nullptr;
        auto front = std::move(pending.front());
        pending.pop_front();
        return front.release();
    }
};

template <class T, class Handle>
T *Object(Handle handle) {
    return reinterpret_cast<T *>(static_cast<uintptr_t>(handle.token));
}

}  // namespace

extern "C" k_gateway_listener k_gateway_listen_v1(uint32_t port, uint32_t capacity, uint32_t budget_ms) {
    if (port > 65535 || !capacity || capacity > 1024 || !budget_ms || budget_ms > 3600000) return {};
    try {
        auto listener = std::make_unique<Listener>();
        listener->network = std::make_shared<Network>();
        if (tcp::Start(&listener->network->state)) return {};
        auto &fd = listener->socket.fd;
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == tcp::invalid_socket || !Nonblocking(fd)) return {};
#ifdef __linux__
        // Rebind after the previous worker exits while closed connections
        // can still be in TIME_WAIT. The listener still owns the port.
        int reuse = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) != 0) return {};
#endif
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<uint16_t>(port));
        auto addr = reinterpret_cast<sockaddr *>(&address);
        if (bind(fd, addr, sizeof address) || listen(fd, static_cast<int>(capacity))) return {};
#ifdef _WIN32
        int size = sizeof address;
#else
        socklen_t size = sizeof address;
#endif
        if (getsockname(fd, addr, &size)) return {};
        listener->port = ntohs(address.sin_port);
        listener->capacity = capacity;
        listener->budget = Millis(budget_ms);
        listener->acceptor = std::thread(&Listener::RunAndNotify, listener.get());
        return { reinterpret_cast<uintptr_t>(listener.release()) };
    } catch (...) { return {}; }
}

extern "C" uint32_t k_gateway_port_v1(k_gateway_listener listener) {
    auto p = Object<Listener>(listener);
    return p ? p->port : 0;
}

extern "C" k_gateway_connection k_gateway_accept_v1(k_gateway_listener listener) {
    auto p = Object<Listener>(listener);
    return { p ? reinterpret_cast<uintptr_t>(p->Next()) : 0 };
}

extern "C" uint32_t k_gateway_peak_pending_v1(k_gateway_listener listener) {
    auto p = Object<Listener>(listener);
    if (!p) return 0;
    std::lock_guard<std::mutex> lock(p->mutex);
    return p->peak_pending;
}

extern "C" void k_gateway_close_listener_v1(k_gateway_listener listener) {
    delete Object<Listener>(listener);
}

extern "C" int32_t k_gateway_connected_v1(k_gateway_connection connection) {
    return connection.token != 0;
}

extern "C" int32_t k_gateway_peer_status_v1(k_gateway_connection connection) {
    auto p = Object<Connection>(connection);
    if (!p) return K_GATEWAY_CLOSED;
    if (Clock::now() >= p->deadline) return K_GATEWAY_DEADLINE;
    char extra;
    auto n = recv(p->socket.fd, &extra, 1, MSG_PEEK);
    if (n > 0) return K_GATEWAY_BAD_REQUEST;
    if (n == 0) return K_GATEWAY_CLOSED;
    auto error = tcp::SocketError();
    return WouldBlock(error) || error == tcp::interrupted_error ? K_GATEWAY_OK : K_GATEWAY_CLOSED;
}

extern "C" int32_t k_gateway_read_v1(k_gateway_connection connection, k_cordis_app_buffer bytes) {
    auto p = Object<Connection>(connection);
    return p ? Transfer(*p, bytes.data, nullptr, bytes.len) : K_GATEWAY_CLOSED;
}

extern "C" int32_t k_gateway_write_v1(k_gateway_connection connection, k_cordis_app_text bytes) {
    auto p = Object<Connection>(connection);
    return p ? Transfer(*p, nullptr, bytes.data, bytes.len) : K_GATEWAY_CLOSED;
}

extern "C" void k_gateway_finish_v1(k_gateway_connection connection, uint8_t status) {
    auto p = Object<Connection>(connection);
    if (!p || status < K_GATEWAY_DONE || status > K_GATEWAY_INFERENCE_ERROR) return;
    if (status != K_GATEWAY_DEADLINE) {
        const uint8_t frame[5] { status, 0, 0, 0, 0 };
        if (Transfer(*p, nullptr, frame, sizeof frame) != K_GATEWAY_DEADLINE) return;
    }
    SendControl(p->socket.fd, K_GATEWAY_DEADLINE);
}

extern "C" void k_gateway_close_connection_v1(k_gateway_connection connection) {
    delete Object<Connection>(connection);
}
