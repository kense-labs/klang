#include "cordis/tcp_stream.h"

#include <cstdio>
#include <cstdlib>

extern "C" const k_cordis_tcp_stream_api *k_test_stream_implementation();

static void event(const char *phase) {
    auto path = std::getenv("K_TEST_CORDIS_APPLICATION_LOG");
    if (auto out = path ? std::fopen(path, "ab") : nullptr) {
        std::fprintf(out, "%s %s\n", phase, K_TEST_CORDIS_PROVIDER_ID);
        std::fclose(out);
    }
}

static int32_t start(void **state) {
    auto result = k_test_stream_implementation()->start(state);
    if (result) return result;
    event("start");
    auto marker = std::getenv("K_TEST_CORDIS_MUTATION_MARKER");
    if (auto flag = marker ? std::fopen(marker, "rb") : nullptr) {
        std::fclose(flag);
        auto path = std::getenv("K_TEST_CORDIS_MUTATION_INPUT");
        auto out = path ? std::fopen(path, "wb") : nullptr;
        if (!out) std::abort();
        std::fputs("changed during provider startup", out);
        std::fclose(out);
    }
    return result;
}

static void stop(void *state) {
    k_test_stream_implementation()->stop(state);
    event("stop");
}

extern "C"
#ifdef _WIN32
__declspec(dllexport)
#endif
const k_cordis_tcp_stream_api *k_cordis_tcp_stream_v1(void) {
    auto base = k_test_stream_implementation();
    static const k_cordis_tcp_stream_api api {
        K_CORDIS_TCP_STREAM_ABI, sizeof(k_cordis_tcp_stream_api), start, stop,
        base->open, base->pump, base->take, base->stats, base->close,
    };
    return &api;
}
