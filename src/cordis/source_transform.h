/* Optional Cordis source-transform ABI. All paths use portable relative names. */
#ifndef K_CORDIS_SOURCE_TRANSFORM_H
#define K_CORDIS_SOURCE_TRANSFORM_H

#include "diagnostics.h" /* k_cordis_text and K_CORDIS_EXPORT */

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_SOURCE_TRANSFORM_ABI 1
#define K_CORDIS_SOURCE_TRANSFORM_ENTRY "k_cordis_source_transform_v1"

struct k_cordis_source_request_v1 {
    /* sources[0] is the entry. Other sources follow in declared input order. */
    const struct k_cordis_source_v1 *sources;
    uint64_t source_count;
    /* The path field holds the manifest input name. Contents are raw bytes. */
    const struct k_cordis_source_v1 *inputs;
    uint64_t input_count;
};

struct k_cordis_source_span_v1 {
    struct k_cordis_text generated_path;
    uint64_t generated_offset;
    uint64_t source_index, source_offset, size;
};

struct k_cordis_source_result_v1 {
    struct k_cordis_text entry;
    const struct k_cordis_source_v1 *modules, *artifacts;
    uint64_t module_count, artifact_count;
    const struct k_cordis_source_span_v1 *spans;
    uint64_t span_count;
};

/* Emit one result synchronously. The host copies all data before returning.
   Spans map equal-length byte ranges. Use disjoint generated ranges. */
typedef int32_t (*k_cordis_emit_source_v1)(void *, const struct k_cordis_source_result_v1 *);

struct k_cordis_source_transform_v1_api {
    uint32_t abi_version, struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    /* Request, callbacks and borrowed bytes are valid only during transform.
       Return zero after an accepted result. A failure publishes no bundle. */
    int32_t (*transform)(void *state, const struct k_cordis_source_request_v1 *,
                         k_cordis_emit_source_v1, void *sink);
    /* Optional suffix. After a failed transform, emit its first diagnostic
       in request source coordinates. Return nonzero when one is emitted. */
    int32_t (*last_error)(void *state, k_cordis_emit_diagnostic_v1, void *sink);
};

#ifdef __cplusplus
}
#endif
#endif
