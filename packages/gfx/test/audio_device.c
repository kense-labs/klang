// Adapted for K.
/* SDL dummy-device coverage. Build target k_audio_test, then run it.
   Does not need a window, GPU, speakers, or an interactive audio session. */
#include "../src/audio/audio_api.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        char error[1024] = { 0 }; \
        k_audio_error((k_audio_bytes){ (uint8_t *)error, sizeof error - 1 }); \
        fprintf(stderr, "line %d: %s: %s\n", __LINE__, #condition, error); \
        exit(1); \
    } \
} while (0)

int main(void) {
    // Packed K fields need not align their f32 slices.
    CHECK(k_audio_open(48000, 1));
    unsigned char packed[9] = { 0 };
    float sample = 0.5f;
    memcpy(packed + 1, &sample, sizeof sample);
    k_audio_sound unaligned = k_audio_create_sound(
        (k_audio_f32_slice){ (float *)(packed + 1), 1 }, 48000, 1);
    CHECK(unaligned.id);
    CHECK(k_audio_play(unaligned, 1, 0, 1, 0).id);
    CHECK(k_audio_render((k_audio_f32_slice){ (float *)(packed + 1), 2 }));
    float pair[2];
    memcpy(pair, packed + 1, sizeof pair);
    CHECK(pair[0] == 0.5f && pair[1] == 0.5f);
    k_audio_close();

    CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "klang-nonexistent-driver"));
    CHECK(!k_audio_open(48000, 0));
    CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    CHECK(SDL_InitSubSystem(SDL_INIT_EVENTS));
    CHECK(k_audio_open(48000, 0));
    CHECK(!k_audio_open(48000, 0));
    float output[2];
    CHECK(!k_audio_render((k_audio_f32_slice){ output, 2 }));

    float pcm[480] = { 0.25f };
    k_audio_sound sound = k_audio_create_sound((k_audio_f32_slice){ pcm, 480 }, 48000, 1);
    CHECK(sound.id);
    k_audio_voice voice = k_audio_play(sound, 1, 0, 1, 0);
    CHECK(voice.id);
    // Observe actual callback progress, with a generous bounded deadline.
    Uint64 deadline = SDL_GetTicks() + 3000;
    while (k_audio_playing(voice) && SDL_GetTicks() < deadline) SDL_Delay(1);
    CHECK(!k_audio_playing(voice));

    float invalid[] = { NAN, INFINITY, -INFINITY };
    for (int i = 0; i < 3; ++i) {
        CHECK(!k_audio_create_sound((k_audio_f32_slice){ &invalid[i], 1 }, 48000, 1).id);
        CHECK(!k_audio_play(sound, invalid[i], 0, 1, 0).id);
        CHECK(!k_audio_play(sound, 1, invalid[i], 1, 0).id);
        CHECK(!k_audio_play(sound, 1, 0, invalid[i], 0).id);
    }
    CHECK(!k_audio_create_sound((k_audio_f32_slice){ NULL, 1 }, 48000, 1).id);
    CHECK(!k_audio_create_sound((k_audio_f32_slice){ pcm, -1 }, 48000, 1).id);
    CHECK(!k_audio_create_sound((k_audio_f32_slice){ pcm, INT64_MAX }, 48000, 1).id);

    // Concurrent callback reads versus main-thread updates, stop and release.
    for (int i = 0; i < 200; ++i) {
        k_audio_sound effect = k_audio_create_sound((k_audio_f32_slice){ pcm, 480 }, 44100, 1);
        CHECK(effect.id);
        k_audio_voice active = k_audio_play(effect, 0.5f, -0.5f, 0.5f, 1);
        CHECK(active.id);
        CHECK(k_audio_set_voice(active, 0.25f, 0.5f, 2));
        if (i % 8 == 0) SDL_Delay(1);
        CHECK(k_audio_release_sound(effect));
        CHECK(!k_audio_playing(active));
    }
    voice = k_audio_play(sound, 1, 0, 1, 1);
    CHECK(voice.id);
    k_audio_close();  // Must synchronize with an active callback before free.
    CHECK(SDL_WasInit(SDL_INIT_EVENTS) != 0);
    CHECK(!k_audio_playing(voice));
    CHECK(k_audio_open(44100, 0));
    CHECK(!k_audio_play(sound, 1, 0, 1, 0).id);
    k_audio_close();
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    puts("dummy device: callback, errors, resource churn, close/reopen passed");
    return 0;
}
