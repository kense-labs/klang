#include "cordis/tcp_policy.h"

#include <stdio.h>
#include <stdlib.h>

static void event(const char *phase) {
    const char *path = getenv("K_TEST_CORDIS_APPLICATION_LOG");
    FILE *out = path ? fopen(path, "ab") : NULL;
    if (out) {
        fprintf(out, "%s policy %s\n", phase, K_TEST_CORDIS_PROVIDER_ID);
        fclose(out);
    }
}

static int32_t start(void **state) {
    *state = NULL;
    event("start");
    return 0;
}

static void stop(void *state) {
    (void)state;
    event("stop");
}

static uint32_t max_capacity(void *state) {
    (void)state;
    return K_TEST_CORDIS_MAX_CAPACITY;
}

const k_cordis_tcp_policy_api *k_cordis_tcp_policy_v1(void) {
    static const k_cordis_tcp_policy_api api = {
        K_CORDIS_TCP_POLICY_ABI, sizeof(k_cordis_tcp_policy_api), start, stop, max_capacity,
    };
    return &api;
}
