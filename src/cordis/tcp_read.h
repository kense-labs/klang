#ifndef K_CORDIS_TCP_READ_H
#define K_CORDIS_TCP_READ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct k_foreign_task;

enum k_tcp_read_status { K_TCP_READ_BYTE, K_TCP_READ_EOF, K_TCP_READ_ERROR };

/* One receive with bounded storage. `error` is the native socket error.
   The same layout is passed to native_task<T> as its fixed result type. */
typedef struct {
    int32_t status;
    int32_t value;
    int32_t error;
} k_tcp_read_result;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    /* socket_token is a connected, blocking TCP socket. Transfers it only on
       success. A host must pin this generation until the returned task's finish. */
    const struct k_foreign_task *(*receive)(void *state, uint64_t socket_token);
} k_cordis_tcp_read_api;

#if defined(_WIN32) && defined(K_CORDIS_TCP_READ_SHARED)
__declspec(dllexport)
#endif
const k_cordis_tcp_read_api *k_cordis_tcp_read_v1(void);

#ifdef __cplusplus
}
#endif
#endif
