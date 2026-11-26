/* The compiler's shader compiler: GLSL in, a gfx shader blob out
   (src/gfx_blob.h). Implemented in C by src/shaderc.c over the copied
   third_party/cute_spirv; always built, with or without SDL, so that a
   program embedding shaders typechecks and emits C the same either way. */

#ifndef K_SHADERC_H
#define K_SHADERC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *blob;   /* malloc'd; set on success */
    size_t size;
    char *error;           /* malloc'd; set on failure, "path:line: error: ..." */
} k_shaderc_result;

/* Compiles `source` for `stage` (K_GFX_STAGE_*). `path` names the file in
   error messages, and #include resolves relative to its directory. */
k_shaderc_result k_shaderc_compile(const char *source, const char *path, int stage);
void k_shaderc_free(k_shaderc_result *r);

#ifdef __cplusplus
}
#endif

#endif
