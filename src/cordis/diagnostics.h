/* Cordis diagnostics ABI v1. Both sides use the host's native C ABI. */
#ifndef K_CORDIS_DIAGNOSTICS_H
#define K_CORDIS_DIAGNOSTICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_DIAGNOSTICS_ABI 1
#define K_CORDIS_DIAGNOSTICS_ENTRY "k_cordis_diagnostics_v1"
#if defined(_WIN32)
#define K_CORDIS_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define K_CORDIS_EXPORT __attribute__((visibility("default")))
#else
#define K_CORDIS_EXPORT
#endif

struct k_cordis_text {
    const char *data;
    uint64_t size;
};

struct k_cordis_source_v1 {
    struct k_cordis_text path;
    struct k_cordis_text contents;
};

#define K_CORDIS_ERROR 1
#define K_CORDIS_WARNING 2

struct k_cordis_diagnostic_v1 {
    int32_t file_index; /* Index into sources, or -1 for a compiler message without a location. */
    int32_t line, column; /* 1-based byte positions. Column 0 denotes the whole line. */
    int32_t end_line, end_column; /* Exclusive end, or both 0. */
    uint32_t severity;
    struct k_cordis_text message;
};

struct k_cordis_snapshot_v1 {
    struct k_cordis_text root_path;
    const struct k_cordis_source_v1 *sources;
    uint64_t source_count;
    const struct k_cordis_diagnostic_v1 *diagnostics;
    uint64_t diagnostic_count;
};

/* The sink copies its argument before returning. A nonzero result accepts
   the diagnostic. Emit only for a location in the snapshot's sources. */
typedef int32_t (*k_cordis_emit_diagnostic_v1)(void *, const struct k_cordis_diagnostic_v1 *);

struct k_cordis_diagnostics_v1_api {
    uint32_t abi_version;
    uint32_t struct_size;
    /* Return 0 to activate. Stop is called once after start, including after
       a failed start. The initial state is null. */
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    /* Input bytes, sink and sink context are borrowed for this call. Keep
       owned state in the instance. Calls are serialized by the host. */
    void (*inspect)(void *state, const struct k_cordis_snapshot_v1 *,
                    k_cordis_emit_diagnostic_v1, void *sink);
};

typedef const struct k_cordis_diagnostics_v1_api *(*k_cordis_diagnostics_entry_v1)(void);

#ifdef __cplusplus
}
#endif
#endif
