// Adapted for K.
/* Packed boundary for stdlib/audio.k and the JIT symbol registry.
   Main-thread callers only; the SDL callback never enters K. */
#ifndef K_AUDIO_API_H
#define K_AUDIO_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 1)
typedef struct { uint8_t *data; int64_t len; } k_audio_bytes;
typedef struct { float *data; int64_t len; } k_audio_f32_slice;
typedef struct { uint64_t id; } k_audio_sound;
typedef struct { uint64_t id; } k_audio_voice;
#pragma pack(pop)

#define K_AUDIO_CONSTANTS(X) \
    X(i32, MAX_SOUNDS, 256) \
    X(i32, MAX_VOICES, 64)
#define K_AUDIO_CONST(type, name, value) enum { K_AUDIO_##name = value };
K_AUDIO_CONSTANTS(K_AUDIO_CONST)
#undef K_AUDIO_CONST

#define K_AUDIO_API(X) \
    X(uint8_t, k_audio_available, (void)) \
    X(uint8_t, k_audio_open, (int64_t sample_rate, uint8_t offline)) \
    X(void, k_audio_close, (void)) \
    X(int64_t, k_audio_error, (k_audio_bytes out)) \
    X(k_audio_sound, k_audio_create_sound, (k_audio_f32_slice samples, int64_t sample_rate, int64_t channels)) \
    X(uint8_t, k_audio_release_sound, (k_audio_sound sound)) \
    X(k_audio_voice, k_audio_play, (k_audio_sound sound, float volume, float pan, float pitch, uint8_t loop)) \
    X(uint8_t, k_audio_playing, (k_audio_voice voice)) \
    X(void, k_audio_stop, (k_audio_voice voice)) \
    X(void, k_audio_stop_all, (void)) \
    X(uint8_t, k_audio_set_voice, (k_audio_voice voice, float volume, float pan, float pitch)) \
    X(uint8_t, k_audio_render, (k_audio_f32_slice out)) \
    X(double, k_audio_voice_position, (k_audio_voice voice)) \
    X(uint8_t, k_audio_seek, (k_audio_voice voice, double seconds))

#define K_AUDIO_PROTO(ret, name, params) ret name params;
K_AUDIO_API(K_AUDIO_PROTO)
#undef K_AUDIO_PROTO
#ifdef __cplusplus
}
#endif
#endif
