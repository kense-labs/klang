#include "tcp_read.h"
#include "runtime/runtime_tasks.h"

#include "tcp_socket.h"

#include <cstring>
#include <mutex>
#include <new>
#include <thread>

namespace {
using namespace klang::cordis::tcp;

struct Receive {
    const Socket socket;
    k_foreign_task_ready binding {};
    std::thread work;
    std::mutex mutex;
    bool completed = false;
    k_task_waker waker {};

    explicit Receive(Socket fd) : socket(fd) {}

    void read(void *destination) {
        unsigned char byte;
        auto [count, error] = Read(socket, &byte, 1);
        k_tcp_read_result result;
        if (count == 1) result = { K_TCP_READ_BYTE, byte, 0 };
        else if (count == 0) result = { K_TCP_READ_EOF, 0, 0 };
        else result = { K_TCP_READ_ERROR, 0, error };
        memcpy(destination, &result, sizeof(result));
        std::lock_guard lock(mutex);
        completed = true;
        k_task_wake(waker);
    }
};

k_task_poll poll_receive(void *frame, void *result) {
    auto &read = *static_cast<Receive *>(frame);
    if (!read.work.joinable()) {
        // The destination is in the compiler's stable task frame. Once this
        // thread starts it may write there until it publishes completion.
        try {
            read.work = std::thread([&read, result] { read.read(result); });
        } catch (...) {
            return K_TASK_POLL_ERROR;
        }
        return K_TASK_POLL_PENDING;
    }
    std::lock_guard lock(read.mutex);
    return read.completed ? K_TASK_POLL_READY : K_TASK_POLL_PENDING;
}

void cancel_receive(void *frame) {
    auto &read = *static_cast<Receive *>(frame);
    if (read.work.joinable()) InterruptReceive(read.socket);
}

bool drain_receive(void *frame) {
    auto &read = *static_cast<Receive *>(frame);
    std::lock_guard lock(read.mutex);
    return !read.work.joinable() || read.completed;
}

bool arm_receive(void *frame, const k_task_waker *waker) {
    auto &read = *static_cast<Receive *>(frame);
    std::lock_guard lock(read.mutex);
    if (read.completed) return false;
    if (waker->signal) read.waker = *waker;
    return true;
}

void finish_receive(void *frame, k_task_state, bool) {
    auto read = static_cast<Receive *>(frame);
    if (read->work.joinable()) read->work.join();
    Close(read->socket);
    delete read;
}

const k_foreign_task *receive(void *, uint64_t token) {
    auto fd = FromToken(token);
    if (fd == invalid_socket) return nullptr;
    auto read = new (std::nothrow) Receive(fd);
    if (!read) return nullptr;
    read->binding = { { K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), sizeof(k_tcp_read_result),
                        read, poll_receive, cancel_receive, drain_receive, finish_receive }, arm_receive };
    return &read->binding.base;
}

}  // namespace

const k_cordis_tcp_read_api *k_cordis_tcp_read_v1(void) {
    static const k_cordis_tcp_read_api api { 1, sizeof(k_cordis_tcp_read_api), Start, Stop, receive };
    return &api;
}
