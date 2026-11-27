#include "value.h"

#include <stdio.h>
#include <stdlib.h>

static void record(const char *event) {
    const char *path = getenv("K_CORDIS_TEST_LOG");
    FILE *log = path ? fopen(path, "ab") : NULL;
    if (!log) return;
    fprintf(log, "%s %s\n", event, K_CORDIS_TEST_PROVIDER);
    fclose(log);
}

static int32_t start(void **state) {
    int32_t *value = malloc(sizeof *value);
    if (!value) return 7;
    *value = K_CORDIS_TEST_VALUE;
    *state = value;
    record("start");
    return K_CORDIS_TEST_START_STATUS;
}

static void stop(void *state) {
    free(state);
    record("stop");
}

static int32_t read_value(void *state) { return *(int32_t *)state; }

const struct k_cordis_test_value_api *k_cordis_test_value_v1(void) {
    static const struct k_cordis_test_value_api api = {
        K_CORDIS_TEST_ABI, sizeof api, start, stop, read_value
    };
    return &api;
}
