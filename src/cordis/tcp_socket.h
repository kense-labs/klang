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
