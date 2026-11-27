#ifndef K_CORDIS_INFERENCE_H
#define K_CORDIS_INFERENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_INFERENCE_ABI 1
#define K_CORDIS_INFERENCE_ENTRY "k_cordis_inference_v1"
#define K_INFERENCE_MAX_RANK 8
#define K_INFERENCE_MAX_ELEMENTS (1024u * 1024u)
#define K_INFERENCE_MAX_MODEL_BYTES (64u * 1024u * 1024u)

enum k_inference_status {
    K_INFERENCE_OK, K_INFERENCE_INVALID_INPUT, K_INFERENCE_BUSY,
    K_INFERENCE_FAILED, K_INFERENCE_CANCELLED
};

typedef struct {
    uint32_t rank;
    uint32_t elements;
    int64_t dimensions[K_INFERENCE_MAX_RANK];
} k_inference_tensor;

typedef struct {
    k_inference_tensor input;
    k_inference_tensor output;
} k_inference_layout;

typedef struct {
    int32_t status;
    uint32_t written;
} k_inference_result;

typedef struct {
    uint64_t reserved_bytes;
    uint64_t in_use_bytes;
    uint64_t peak_in_use_bytes;
} k_inference_arena;

struct k_foreign_task;
typedef struct k_inference_request k_inference_request;

/* A generation exposes one contiguous f32 input and output with fixed
   shapes. The host drives its handles on one thread. init_model reads
   verified model bytes and a locked runtime path once before publication.
   On error, model_error supplies a message until the next initialization.
   The loader keeps the generation alive during every request and task. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    int32_t (*init_model)(void *state, const uint8_t *data, size_t length, const char *runtime_path);
    const char *(*model_error)(void *state);
    k_inference_layout (*layout)(void *state);
    /* Copy input during this call. A nonzero result transfers no request. */
    int32_t (*request)(void *state, const float *input, size_t elements, k_inference_request **out);
    /* Claim one task for a request. This task has a k_inference_result. */
    const struct k_foreign_task *(*task)(void *state, k_inference_request *request);
    /* Copy after successful task completion. Returns the count copied,
       zero if pending, failed or too short. It never copies a partial tensor. */
    uint32_t (*output)(void *state, k_inference_request *request, float *out, size_t elements);
    /* Release the caller's request. A claimed task still drains and
       releases its own reference, and close requests cancellation. */
    void (*close)(void *state, k_inference_request *request);
    /* Optional. Returns one when the provider can measure the session's
       CPU arena, including while the inference worker is active. */
    int32_t (*arena)(void *state, k_inference_arena *out);
} k_cordis_inference_api;

#if defined(_WIN32) && defined(K_CORDIS_INFERENCE_BUILD)
__declspec(dllexport)
#endif
const k_cordis_inference_api *k_cordis_inference_v1(void);

#ifdef __cplusplus
}
#endif
#endif
