#ifndef K_CORDIS_GATEWAY_NET_H
#define K_CORDIS_GATEWAY_NET_H

#include "application.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(K_CORDIS_APPLICATION_BUILD)
#define K_GATEWAY_API __declspec(dllexport)
#else
#define K_GATEWAY_API
#endif

typedef struct { uint64_t token; } k_gateway_listener;
typedef struct { uint64_t token; } k_gateway_connection;
enum {
    K_GATEWAY_OK = 0,
    K_GATEWAY_DATA = 1,
    K_GATEWAY_DONE = 2,
    K_GATEWAY_BAD_REQUEST = 3,
    K_GATEWAY_DEADLINE = 4,
    K_GATEWAY_BUSY = 5,
    K_GATEWAY_INFERENCE_ERROR = 6,
    K_GATEWAY_CLOSED = 7,   /* Internal I/O status. */
};

/* Bind loopback and start a bounded FIFO. Zero requests an ephemeral port.
   Each dequeued connection owns its socket and may outlive the listener. */
K_GATEWAY_API k_gateway_listener k_gateway_listen_v1(uint32_t port, uint32_t capacity, uint32_t budget_ms);
K_GATEWAY_API uint32_t k_gateway_port_v1(k_gateway_listener listener);
K_GATEWAY_API k_gateway_connection k_gateway_accept_v1(k_gateway_listener listener);
K_GATEWAY_API void k_gateway_close_listener_v1(k_gateway_listener listener);
K_GATEWAY_API uint32_t k_gateway_peak_pending_v1(k_gateway_listener listener);

/* The client keeps its write half open until it sees a terminal frame.
   Status probes also reject any bytes already queued after the request. */
K_GATEWAY_API int32_t k_gateway_peer_status_v1(k_gateway_connection connection);
K_GATEWAY_API int32_t k_gateway_connected_v1(k_gateway_connection connection);
K_GATEWAY_API int32_t k_gateway_read_v1(k_gateway_connection connection, k_cordis_app_buffer bytes);
K_GATEWAY_API int32_t k_gateway_write_v1(k_gateway_connection connection, k_cordis_app_text bytes);
K_GATEWAY_API void k_gateway_finish_v1(k_gateway_connection connection, uint8_t status);
K_GATEWAY_API void k_gateway_close_connection_v1(k_gateway_connection connection);

#undef K_GATEWAY_API
#ifdef __cplusplus
}
#endif
#endif
