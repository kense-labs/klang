/* Optional Cordis TypeScript project contracts. The runner owns I/O. */
#ifndef K_CORDIS_TYPESCRIPT_PROJECT_H
#define K_CORDIS_TYPESCRIPT_PROJECT_H

#include "diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_TYPESCRIPT_ABI 1
#define K_CORDIS_TYPESCRIPT_FRONTEND_ENTRY "k_cordis_typescript_frontend_v1"
#define K_CORDIS_TYPESCRIPT_PROJECT_ENTRY "k_cordis_typescript_project_v1"

typedef int32_t (*k_cordis_ts_emit_text_v1)(void *, struct k_cordis_text);

struct k_cordis_ts_compiled_v1 {
    struct k_cordis_text javascript, source_map, error;
};

/* The emitter copies the result before returning nonzero to accept it.
   A result contains either emitted ESM and a source map, or a diagnostic. */
typedef int32_t (*k_cordis_ts_emit_module_v1)(void *, const struct k_cordis_ts_compiled_v1 *);
typedef int32_t (*k_cordis_ts_compile_v1)(void *, struct k_cordis_source_v1,
                                         k_cordis_ts_emit_module_v1, void *);

struct k_cordis_typescript_frontend_v1_api {
    uint32_t abi_version, struct_size;
    int32_t (*start)(void **);
    void (*stop)(void *);
    /* Initialize from the verified compiler input. Zero denotes success. */
    int32_t (*configure)(void *, struct k_cordis_text, k_cordis_ts_emit_text_v1, void *);
    k_cordis_ts_compile_v1 compile;
};

/* Read a project-relative module. On success expose its canonical relative
   name and bytes through run(). On failure expose a diagnostic until the
   next read. Return zero on success. */
typedef int32_t (*k_cordis_ts_read_v1)(void *, struct k_cordis_text,
                                      struct k_cordis_source_v1 *, struct k_cordis_text *);

/* Send bytes to stdout (1) or stderr (2). Return nonzero on success. */
typedef int32_t (*k_cordis_ts_write_v1)(void *, int32_t, struct k_cordis_text);

struct k_cordis_ts_project_request_v1 {
    struct k_cordis_text entry;
    void *host;
    k_cordis_ts_read_v1 read;
    k_cordis_ts_write_v1 write;
    k_cordis_ts_compile_v1 compile;
    k_cordis_ts_emit_text_v1 error;
};

struct k_cordis_typescript_project_v1_api {
    uint32_t abi_version, struct_size;
    int32_t (*start)(void **);
    void (*stop)(void *);
    /* Execute synchronously. The request and callbacks are borrowed. */
    int32_t (*run)(void *, const struct k_cordis_ts_project_request_v1 *);
};

#ifdef __cplusplus
}
#endif
#endif
