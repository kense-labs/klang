/* The shader blob `embed_shader` produces: one shader stage, compiled from
   GLSL at compile time into every format SDL_GPU takes, plus the reflection
   that creating an SDL_GPU shader or pipeline needs. The compiler writes it
   (src/shaderc.c) and embeds it in the program as static data; the gfx layer
   (src/gfx/) reads it back. Shared by both, so it is plain C.

   Layout, little-endian and unaligned (a blob sits in a byte array):

       k_gfx_blob_header
       uint32_t uniform_sizes[num_uniform_buffers]   std140 size, by binding
       k_gfx_blob_input inputs[num_inputs]          vertex stage, by location
       the SPIR-V words, the MSL text and the HLSL text, at the offsets the
       header gives; each text is followed by a NUL its size leaves out.

   Readers copy the header out with memcpy rather than casting a pointer to
   it: nothing aligns the array holding the blob. */

#ifndef K_GFX_BLOB_H
#define K_GFX_BLOB_H

#include <stdint.h>

#define K_GFX_BLOB_MAGIC "GSHD"
#define K_GFX_BLOB_VERSION 1

enum {
    K_GFX_STAGE_VERTEX = 0,
    K_GFX_STAGE_FRAGMENT = 1,
    K_GFX_STAGE_COMPUTE = 2,
};

/* A vertex input's type, as cute_spirv reflects it (CSPV_DataType). */
enum {
    K_GFX_INPUT_INT = 1, K_GFX_INPUT_UINT = 2, K_GFX_INPUT_FLOAT = 3,
    K_GFX_INPUT_INT2 = 4, K_GFX_INPUT_UINT2 = 5, K_GFX_INPUT_FLOAT2 = 6,
    K_GFX_INPUT_INT3 = 7, K_GFX_INPUT_UINT3 = 8, K_GFX_INPUT_FLOAT3 = 9,
    K_GFX_INPUT_INT4 = 10, K_GFX_INPUT_UINT4 = 11, K_GFX_INPUT_FLOAT4 = 12,
    K_GFX_INPUT_MAT4 = 13,
};

#pragma pack(push, 1)
typedef struct {
    char magic[4];
    uint16_t version;
    uint8_t stage;
    uint8_t num_inputs;
    uint32_t local_size[3];         /* compute only */
    /* In a graphics stage every storage resource is read-only. */
    uint16_t num_samplers, num_storage_textures_ro, num_storage_textures_rw,
             num_storage_buffers_ro, num_storage_buffers_rw, num_uniform_buffers;
    uint32_t spirv_offset, spirv_size;
    uint32_t msl_offset, msl_size;
    uint32_t hlsl_offset, hlsl_size;
} k_gfx_blob_header;

typedef struct {
    uint8_t location;
    uint8_t type;                   /* K_GFX_INPUT_* */
} k_gfx_blob_input;
#pragma pack(pop)

#endif
