#ifndef K_CORDIS_TCP_STREAM_H
#define K_CORDIS_TCP_STREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_TCP_STREAM_ABI 1
#define K_CORDIS_TCP_STREAM_ENTRY "k_cordis_tcp_stream_v1"
#define K_TCP_STREAM_CHUNK_BYTES 4096
#define K_TCP_STREAM_MAX_CAPACITY 1024

struct k_foreign_task;
typedef struct k_tcp_stream_reader k_tcp_stream_reader;

enum k_tcp_stream_status {
    K_TCP_STREAM_PENDING, K_TCP_STREAM_DATA, K_TCP_STREAM_EOF,
    K_TCP_STREAM_ERROR, K_TCP_STREAM_CANCELLED
};

/* DATA means count > 0. ERROR carries the native socket error. */
typedef struct {
    int32_t status;
    int32_t error;
    uint32_t count;
} k_tcp_stream_read_result;

typedef struct {
    uint64_t submitted_reads;
    uint32_t queued_chunks;
    uint32_t read_in_flight; /* The worker currently owns the socket read. */
} k_tcp_stream_stats;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    /* Calls and task callbacks use one driving thread. On success, open
       consumes a connected, blocking TCP socket and returns an owned reader.
       The host pins the provider until both the reader and task are released. */
    k_tcp_stream_reader *(*open)(void *state, uint64_t socket, uint32_t capacity);
    /* Once per reader. Transfers the task to the caller. Its fixed result is
       k_tcp_stream_read_result with count 0 and the terminal status. */
    const struct k_foreign_task *(*pump)(void *state, k_tcp_stream_reader *reader);
    /* Copy up to capacity bytes from the front chunk. A partial read keeps
       its queue slot. destination must provide at least capacity bytes,
       with capacity > 0. Buffered data precedes EOF and socket errors. */
    k_tcp_stream_read_result (*take)(void *state, k_tcp_stream_reader *reader,
                                     uint8_t *destination, uint32_t capacity);
    k_tcp_stream_stats (*stats)(void *state, k_tcp_stream_reader *reader);
    /* Release the reader. If a pump is active this requests cancellation.
       The caller still drives the pump to terminal to drain the read and
       release the socket. A reader can also outlive its finished pump. */
    void (*close)(void *state, k_tcp_stream_reader *reader);
} k_cordis_tcp_stream_api;

#if defined(_WIN32) && defined(K_CORDIS_TCP_READ_SHARED)
__declspec(dllexport)
#endif
const k_cordis_tcp_stream_api *k_cordis_tcp_stream_v1(void);

#ifdef __cplusplus
}
#endif
#endif
