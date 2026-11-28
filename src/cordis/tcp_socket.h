#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <limits>

namespace klang::cordis::tcp {

#ifdef _WIN32
using Socket = SOCKET;
inline constexpr Socket invalid_socket = INVALID_SOCKET;
inline void Close(Socket fd) { closesocket(fd); }
inline void InterruptReceive(Socket fd) { shutdown(fd, SD_RECEIVE); }
inline int SocketError() { return WSAGetLastError(); }
inline constexpr int interrupted_error = WSAEINTR;
inline constexpr int worker_error = WSAENOBUFS;
inline constexpr int invalid_argument_error = WSAEINVAL;
#else
using Socket = int;
inline constexpr Socket invalid_socket = -1;
inline void Close(Socket fd) { close(fd); }
inline void InterruptReceive(Socket fd) { shutdown(fd, SHUT_RD); }
inline int SocketError() { return errno; }
inline constexpr int interrupted_error = EINTR;
inline constexpr int worker_error = EAGAIN;
inline constexpr int invalid_argument_error = EINVAL;
#endif

inline Socket FromToken(uint64_t token) {
    if (token > (uint64_t)std::numeric_limits<Socket>::max()) return invalid_socket;
    return static_cast<Socket>(token);
}

struct ReadResult {
    int count;
    int error;
};

inline ReadResult Read(Socket fd, void *storage, int capacity) {
    ReadResult result;
    do {
        result.count = (int)recv(fd, static_cast<char *>(storage), capacity, 0);
        result.error = result.count < 0 ? SocketError() : 0;
    } while (result.error == interrupted_error);
    return result;
}

struct CancelableRead {
#ifdef _WIN32
    WSAEVENT cancel_event = WSACreateEvent();
    WSAEVENT net_event = WSACreateEvent();

    CancelableRead() = default;
    ~CancelableRead() {
        if (cancel_event != WSA_INVALID_EVENT) WSACloseEvent(cancel_event);
        if (net_event != WSA_INVALID_EVENT) WSACloseEvent(net_event);
    }
    CancelableRead(const CancelableRead &) = delete;
    CancelableRead &operator=(const CancelableRead &) = delete;

    void Interrupt(Socket fd) {
        if (cancel_event != WSA_INVALID_EVENT) WSASetEvent(cancel_event);
        InterruptReceive(fd);
    }

    ReadResult Read(Socket fd, void *storage, int capacity) {
        if (cancel_event == WSA_INVALID_EVENT || net_event == WSA_INVALID_EVENT) {
            return klang::cordis::tcp::Read(fd, storage, capacity);
        }
        if (WSAEventSelect(fd, net_event, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
            return { -1, SocketError() };
        }
        bool saw_close = false;
        int close_error = 0;
        for (;;) {
            if (WSAWaitForMultipleEvents(1, &cancel_event, FALSE, 0, FALSE) == WSA_WAIT_EVENT_0) {
                return { -1, WSAECONNABORTED };
            }
            int count = (int)recv(fd, static_cast<char *>(storage), capacity, 0);
            if (count >= 0) return { count, 0 };
            int err = SocketError();
            if (err == interrupted_error) continue;
            if (err != WSAEWOULDBLOCK) return { -1, err };
            if (saw_close) return close_error ? ReadResult { -1, close_error } : ReadResult { 0, 0 };

            WSAEVENT events[2] = { cancel_event, net_event };
            DWORD wait = WSAWaitForMultipleEvents(2, events, FALSE, WSA_INFINITE, FALSE);
            if (wait == WSA_WAIT_EVENT_0) return { -1, WSAECONNABORTED };
            if (wait != WSA_WAIT_EVENT_0 + 1) return { -1, SocketError() };
            WSANETWORKEVENTS ne {};
            if (WSAEnumNetworkEvents(fd, net_event, &ne) == 0 && (ne.lNetworkEvents & FD_CLOSE)) {
                saw_close = true;
                close_error = ne.iErrorCode[FD_CLOSE_BIT];
            }
        }
    }
#else
    void Interrupt(Socket fd) { InterruptReceive(fd); }
    ReadResult Read(Socket fd, void *storage, int capacity) {
        return klang::cordis::tcp::Read(fd, storage, capacity);
    }
#endif
};

inline int32_t Start(void **state) {
    *state = nullptr;
#ifdef _WIN32
    WSADATA version;
    auto status = WSAStartup(MAKEWORD(2, 2), &version);
    static char socket_ready;
    if (!status) *state = &socket_ready;
    return status;
#else
    return 0;
#endif
}

inline void Stop(void *state) {
#ifdef _WIN32
    if (state) WSACleanup();
#else
    (void)state;
#endif
}

}  // namespace klang::cordis::tcp
