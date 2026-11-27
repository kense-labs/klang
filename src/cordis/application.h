#ifndef K_CORDIS_APPLICATION_H
#define K_CORDIS_APPLICATION_H

#include "inference.h"
#include "tcp_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(K_CORDIS_APPLICATION_BUILD)
#define K_CORDIS_APP_API __declspec(dllexport)
#else
#define K_CORDIS_APP_API
#endif

typedef struct { uint64_t token; } k_cordis_app;
typedef struct { uint64_t token; } k_cordis_app_socket;
typedef struct { uint64_t token; } k_cordis_app_reader;
typedef struct { uint64_t token; } k_cordis_app_task;
typedef struct { uint64_t token; } k_cordis_app_inference;
typedef struct { const uint8_t *data; int64_t len; } k_cordis_app_text;
typedef struct { uint8_t *data; int64_t len; } k_cordis_app_buffer;
typedef struct { const float *data; int64_t len; } k_cordis_app_input;
typedef struct { float *data; int64_t len; } k_cordis_app_output;

typedef struct {
    k_inference_arena active;
    uint64_t retired_reserved_bytes;
    uint64_t last_activation_reserved_bytes;
    uint32_t active_measured;
    uint32_t retired_count;
    uint32_t retired_measured;
    uint32_t last_activation_measured;
} k_inference_workspace;

/* The application drives its handles on one thread. Open verifies the
   default tcp.stream graph and its optional tcp.policy dependency and
   returns an owned handle or token zero.
   Select uses the startup catalog and verifies a lock for the requested
   provider. Returns 1 for replacement, 0 for the same graph, -1 on error.
   A failed selection leaves the current graph and generation active. */
K_CORDIS_APP_API k_cordis_app k_cordis_app_open_v1(k_cordis_app_text manifest, k_cordis_app_text lock);
K_CORDIS_APP_API k_cordis_app k_cordis_app_open_inference_v1(k_cordis_app_text manifest, k_cordis_app_text lock);
K_CORDIS_APP_API int32_t k_cordis_app_select_v1(k_cordis_app app, k_cordis_app_text provider,
                                              k_cordis_app_text lock);
K_CORDIS_APP_API void k_cordis_app_close_v1(k_cordis_app app);
K_CORDIS_APP_API int32_t k_cordis_app_ready_v1(k_cordis_app app);

/* Requires a Linux cgroup v2 memory.max no larger than bytes and a zero
   effective swap limit. Returns 1 when verified, 0 with an error otherwise. */
K_CORDIS_APP_API int32_t k_cordis_app_require_memory_budget_v1(uint64_t bytes);

/* Text functions return the byte count and copy only if the whole result
   fits. Text has no trailing NUL. The error JSON belongs to the calling
   thread and describes its last fallible operation. */
K_CORDIS_APP_API int64_t k_cordis_app_error_v1(k_cordis_app_buffer out);
K_CORDIS_APP_API int64_t k_cordis_app_graph_v1(k_cordis_app app, k_cordis_app_buffer out);
K_CORDIS_APP_API int64_t k_cordis_app_provider_v1(k_cordis_app app, k_cordis_app_buffer out);
K_CORDIS_APP_API int64_t k_cordis_app_reader_provider_v1(k_cordis_app_reader reader, k_cordis_app_buffer out);

/* Adopt consumes a connected, blocking socket even on failure. A successful
   reader and its pump hold independent generation pins. Failed adopt or
   pump returns token zero. Close consumes a reader. Discard consumes a
   pump that has not been passed to the K task driver. */
K_CORDIS_APP_API k_cordis_app_reader k_cordis_app_adopt_v1(k_cordis_app app, k_cordis_app_socket socket,
                                                          uint32_t capacity);
K_CORDIS_APP_API k_cordis_app_task k_cordis_app_pump_v1(k_cordis_app_reader reader);
K_CORDIS_APP_API int32_t k_cordis_app_reader_ready_v1(k_cordis_app_reader reader);
K_CORDIS_APP_API int32_t k_cordis_app_task_ready_v1(k_cordis_app_task task);
K_CORDIS_APP_API k_tcp_stream_read_result k_cordis_app_take_v1(k_cordis_app_reader reader,
                                                               k_cordis_app_buffer out);
K_CORDIS_APP_API k_tcp_stream_stats k_cordis_app_stats_v1(k_cordis_app_reader reader);
K_CORDIS_APP_API void k_cordis_app_close_reader_v1(k_cordis_app_reader reader);
K_CORDIS_APP_API void k_cordis_app_discard_task_v1(k_cordis_app_task task);

/* Inference is available with an inference root or dependency in the graph.
   Request copies the input. The result tensor stays in the owned request
   until it is copied out or released. Zero handles indicate failure. */
K_CORDIS_APP_API k_inference_layout k_cordis_app_inference_layout_v1(k_cordis_app app);
/* Reports the active arena, live retired generations, and the combined
   reserved bytes sampled before the last successful inference publication.
   The measurement flags distinguish unavailable telemetry from zero bytes. */
K_CORDIS_APP_API k_inference_workspace k_cordis_app_inference_workspace_v1(k_cordis_app app);
K_CORDIS_APP_API k_cordis_app_inference k_cordis_app_infer_v1(k_cordis_app app, k_cordis_app_input input);
K_CORDIS_APP_API int32_t k_cordis_app_inference_ready_v1(k_cordis_app_inference request);
K_CORDIS_APP_API k_cordis_app_task k_cordis_app_inference_task_v1(k_cordis_app_inference request);
K_CORDIS_APP_API int32_t k_cordis_app_inference_task_ready_v1(k_cordis_app_task task);
K_CORDIS_APP_API void k_cordis_app_discard_inference_task_v1(k_cordis_app_task task);
K_CORDIS_APP_API uint32_t k_cordis_app_inference_output_v1(k_cordis_app_inference request, k_cordis_app_output out);
K_CORDIS_APP_API void k_cordis_app_close_inference_v1(k_cordis_app_inference request);

#undef K_CORDIS_APP_API

#ifdef __cplusplus
}
#endif
#endif
