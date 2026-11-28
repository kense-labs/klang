#include "tcp_stream.h"
#include "tcp_socket.h"
#include "runtime/runtime_tasks.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace klang::cordis::tcp;

struct k_tcp_stream_reader {
    struct Chunk {
        uint32_t size;
        std::array<uint8_t, K_TCP_STREAM_CHUNK_BYTES> bytes;
    };

    enum class ReadState { idle, submitted, receiving, complete };

    Socket socket;
    std::vector<Chunk> chunks;
    size_t head = 0;
    uint32_t queued = 0;
    uint32_t head_offset = 0;
    uint64_t submissions = 0;
    k_tcp_stream_read_result terminal { K_TCP_STREAM_PENDING, 0, 0 };
    bool reader_open = true;
    bool pump_claimed = false;
    bool pump_live = false;
    bool cancelled = false;
    k_foreign_task_ready binding {};

    // One reserved tail slot crosses to the worker at a time. Only its
    // payload bytes are written outside the mutex. Take uses the ready slots
    // on the driving thread, so it never accesses the reserved slot.
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    ReadState read = ReadState::idle;
    size_t reserved = 0;
    ReadResult received {};
    bool stop = false;
    CancelableRead io;
    k_task_waker waker {};

    k_tcp_stream_reader(Socket socket, uint32_t capacity) : socket(socket), chunks(capacity) {}
    ~k_tcp_stream_reader() { CloseSocket(); }

    void CloseSocket() {
        if (socket != invalid_socket) Close(std::exchange(socket, invalid_socket));
    }

    void Work() {
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stop || read == ReadState::submitted; });
            if (stop) return;
            auto &destination = chunks[reserved].bytes;
            read = ReadState::receiving;
            lock.unlock();
            auto result = io.Read(socket, destination.data(), (int)destination.size());
            lock.lock();
            received = result;
            read = ReadState::complete;
            k_task_wake(waker);
        }
    }

    k_task_poll Poll(void *result) {
        std::lock_guard lock(mutex);
        if (cancelled) {
            if (read == ReadState::receiving) return K_TASK_POLL_PENDING;
            terminal = { K_TCP_STREAM_CANCELLED, 0, 0 };
        } else {
            if (!worker.joinable()) {
                try {
                    worker = std::thread([this] { Work(); });
                } catch (...) {
                    terminal = { K_TCP_STREAM_ERROR, worker_error, 0 };
                }
            }
            if (read == ReadState::complete) {
                read = ReadState::idle;
                if (received.count > 0) {
                    chunks[reserved].size = (uint32_t)received.count;
                    ++queued;
                } else if (received.count == 0) {
                    terminal = { K_TCP_STREAM_EOF, 0, 0 };
                } else {
                    terminal = { K_TCP_STREAM_ERROR, received.error, 0 };
                }
            }
        }
        if (terminal.status != K_TCP_STREAM_PENDING) {
            memcpy(result, &terminal, sizeof(terminal));
            return K_TASK_POLL_READY;
        }
        if (read == ReadState::idle && queued < chunks.size()) {
            reserved = (head + queued) % chunks.size();
            read = ReadState::submitted;
            ++submissions;
            wake.notify_one();
        }
        return K_TASK_POLL_PENDING;
    }

    void Cancel() {
        std::lock_guard lock(mutex);
        if (cancelled) return;
        cancelled = true;
        queued = head_offset = 0;
        if (read == ReadState::receiving) {
            io.Interrupt(socket);
        } else {
            // A submitted read that the worker has not claimed can be revoked.
            read = ReadState::idle;
        }
        k_task_wake(waker);
    }

    bool Drain() {
        std::lock_guard lock(mutex);
        return read != ReadState::receiving && read != ReadState::submitted;
    }

    bool Arm(const k_task_waker &next) {
        std::lock_guard lock(mutex);
        if (read == ReadState::complete) return false;
        if (read == ReadState::idle && (cancelled || queued < chunks.size())) return false;
        if (next.signal) waker = next;
        return true;
    }

    void Finish(k_task_state state) {
        {
            std::lock_guard lock(mutex);
            read = ReadState::idle;
            stop = true;
        }
        wake.notify_one();
        if (worker.joinable()) worker.join();
        CloseSocket();
        if (state == K_TASK_CANCELLED) {
            terminal = { K_TCP_STREAM_CANCELLED, 0, 0 };
            queued = head_offset = 0;
        } else if (state == K_TASK_FAILED && terminal.status == K_TCP_STREAM_PENDING) {
            terminal = { K_TCP_STREAM_ERROR, invalid_argument_error, 0 };
        }
        pump_live = false;
    }

    k_tcp_stream_read_result Take(uint8_t *destination, uint32_t capacity) {
        if (!destination || !capacity) return { K_TCP_STREAM_ERROR, invalid_argument_error, 0 };
        if (!queued) return terminal;
        auto &chunk = chunks[head];
        auto count = std::min(capacity, chunk.size - head_offset);
        memcpy(destination, chunk.bytes.data() + head_offset, count);
        head_offset += count;
        if (head_offset == chunk.size) {
            if (queued == chunks.size()) k_task_wake(waker);
            head = (head + 1) % chunks.size();
            head_offset = 0;
            --queued;
        }
        return { K_TCP_STREAM_DATA, 0, count };
    }

    k_tcp_stream_stats Stats() {
        std::lock_guard lock(mutex);
        return { submissions, queued, read == ReadState::receiving };
    }
};

namespace {

k_tcp_stream_reader *open_stream(void *, uint64_t token, uint32_t capacity) {
    auto socket = FromToken(token);
    if (socket == invalid_socket || !capacity || capacity > K_TCP_STREAM_MAX_CAPACITY) return nullptr;
    try {
        return new k_tcp_stream_reader(socket, capacity);
    } catch (...) {
        return nullptr;
    }
}

const k_foreign_task *pump_stream(void *, k_tcp_stream_reader *stream) {
    if (stream->pump_claimed) return nullptr;
    stream->pump_claimed = stream->pump_live = true;
    stream->binding = {
        {
            K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), sizeof(k_tcp_stream_read_result), stream,
            [](void *frame, void *result) { return static_cast<k_tcp_stream_reader *>(frame)->Poll(result); },
            [](void *frame) { static_cast<k_tcp_stream_reader *>(frame)->Cancel(); },
            [](void *frame) { return static_cast<k_tcp_stream_reader *>(frame)->Drain(); },
            [](void *frame, k_task_state state, bool) {
                auto stream = static_cast<k_tcp_stream_reader *>(frame);
                stream->Finish(state);
                if (!stream->reader_open) delete stream;
            },
        },
        [](void *frame, const k_task_waker *waker) {
            return static_cast<k_tcp_stream_reader *>(frame)->Arm(*waker);
        },
    };
    return &stream->binding.base;
}

k_tcp_stream_read_result take_stream(void *, k_tcp_stream_reader *stream, uint8_t *destination, uint32_t capacity) {
    return stream->Take(destination, capacity);
}

k_tcp_stream_stats stats_stream(void *, k_tcp_stream_reader *stream) { return stream->Stats(); }

void close_stream(void *, k_tcp_stream_reader *stream) {
    if (stream->pump_live) {
        stream->reader_open = false;
        stream->Cancel();
    } else {
        delete stream;
    }
}

}  // namespace

const k_cordis_tcp_stream_api *k_cordis_tcp_stream_v1(void) {
    static const k_cordis_tcp_stream_api api { K_CORDIS_TCP_STREAM_ABI, sizeof(k_cordis_tcp_stream_api),
        Start, Stop, open_stream, pump_stream, take_stream, stats_stream, close_stream };
    return &api;
}
