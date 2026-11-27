#include "cordis/diagnostics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record(const char *event) {
    const char *path = getenv("K_CORDIS_TEST_LOG");
    FILE *log = path ? fopen(path, "ab") : NULL;
    if (!log) return;
    fprintf(log, "%s %s\n", event, K_CORDIS_TEST_PROVIDER);
    fclose(log);
}

static int32_t start(void **state) {
    *state = malloc(1);
    record("start");
    return *state ? K_CORDIS_TEST_START_STATUS : 7;
}

static void stop(void *state) {
    free(state);
    record("stop");
}

static void inspect(void *state, const struct k_cordis_snapshot_v1 *snapshot,
                    k_cordis_emit_diagnostic_v1 emit, void *context) {
    if (!state || !snapshot->root_path.size || !snapshot->source_count ||
        !snapshot->sources[0].contents.size) return;
    const char message[] = "diagnostics from " K_CORDIS_TEST_PROVIDER;
    struct k_cordis_diagnostic_v1 d = { -1, 1, 1, 0, 0, K_CORDIS_WARNING,
                                       { message, sizeof message - 1 } };
    if (emit(context, &d)) return; /* The sink must reject a missing file. */
    d.file_index = 0;
    if (getenv("K_CORDIS_TEST_REJECT")) d.severity = K_CORDIS_ERROR;
    emit(context, &d);
    const char *replace = getenv("K_CORDIS_TEST_CHANGE_INPUT");
    FILE *input = replace ? fopen(replace, "wb") : NULL;
    if (input) {
        fputs("changed while the provider was active", input);
        fclose(input);
    }
    if (snapshot->diagnostic_count) {
        const char prefix[] = "observed compiler diagnostic";
        const struct k_cordis_diagnostic_v1 *original = &snapshot->diagnostics[0];
        if (original->severity != K_CORDIS_ERROR || !original->message.size) return;
        d.message.data = prefix;
        d.message.size = sizeof prefix - 1;
        emit(context, &d);
    }
}

K_CORDIS_EXPORT const struct k_cordis_diagnostics_v1_api *k_cordis_diagnostics_v1(void) {
    static const struct k_cordis_diagnostics_v1_api api = {
        K_CORDIS_TEST_ABI, sizeof api, start, stop, inspect
    };
    return &api;
}
