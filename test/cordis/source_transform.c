#include "cordis/source_transform.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT(s) { (s), sizeof(s) - 1 }

static void log_event(const char *event) {
    const char *path = getenv("K_CORDIS_SOURCE_TEST_LOG");
    if (!path) return;
    FILE *log = fopen(path, "ab");
    if (log) {
        fprintf(log, "%s\n", event);
        fclose(log);
    }
}

static int32_t start(void **state) {
    (void)state;
    log_event("start");
    return 0;
}

static void stop(void *state) {
    (void)state;
    log_event("stop");
}

static int32_t equals(struct k_cordis_text text, const char *expected) {
    return text.size == strlen(expected) && memcmp(text.data, expected, (size_t)text.size) == 0;
}

static int32_t transform(void *state, const struct k_cordis_source_request_v1 *request,
                         k_cordis_emit_source_v1 emit, void *context) {
    (void)state;
    log_event("transform");
    const char prefix[] = "import helper;\nfn main() { print(";
    const char suffix[] = "); }\n";
    const char header[] = "show\n";
    const char asset_data[] = { 0, 2, (char)255 };
    if (request->source_count != 2 || request->input_count != 1 ||
        !equals(request->inputs[0].path, "rules")) return 2;
    const struct k_cordis_text source = request->sources[0].contents;
    const struct k_cordis_text helper = request->sources[1].contents;
    if (source.size <= sizeof(header) - 1 || memcmp(source.data, header, sizeof(header) - 1)) return 3;
    size_t length = (size_t)source.size - (sizeof(header) - 1);
    char *module = malloc(sizeof(prefix) - 1 + length + sizeof(suffix) - 1);
    if (!module) return 4;
    memcpy(module, prefix, sizeof(prefix) - 1);
    memcpy(module + sizeof(prefix) - 1, source.data + sizeof(header) - 1, length);
    memcpy(module + sizeof(prefix) - 1 + length, suffix, sizeof(suffix) - 1);
    struct k_cordis_source_v1 modules[] = {
        { TEXT("main.k"), { module, sizeof(prefix) - 1 + length + sizeof(suffix) - 1 } },
        { TEXT("helper.k"), helper },
    };
    struct k_cordis_source_v1 assets[] = {
        { TEXT("guest/data.bin"), { asset_data, sizeof(asset_data) } },
    };
    struct k_cordis_source_span_v1 spans[] = {
        { TEXT("main.k"), sizeof(prefix) - 1, 0, sizeof(header) - 1, length },
        { TEXT("helper.k"), 0, 1, 0, helper.size },
    };
    const struct k_cordis_text rules = request->inputs[0].contents;
    if (equals(rules, "escape")) {
        const struct k_cordis_text bad = TEXT("../escape.bin");
        assets[0].path = bad;
    }
    if (equals(rules, "bad-map")) spans[0].source_offset = source.size;
    struct k_cordis_source_result_v1 result = { TEXT("main.k"), modules, assets, 2, 1, spans, 2 };
    const int32_t accepted = emit(context, &result);
    // Simulate input drift after the host has consumed the selected bytes.
    const char *changed = getenv("K_CORDIS_SOURCE_TEST_CHANGE");
    if (changed) {
        FILE *file = fopen(changed, "ab");
        if (file) {
            fputc('!', file);
            fclose(file);
        }
    }
    free(module);
    return accepted ? 0 : 5;
}

K_CORDIS_EXPORT const struct k_cordis_source_transform_v1_api *k_cordis_source_transform_v1(void) {
    static const struct k_cordis_source_transform_v1_api api = {
        K_CORDIS_SOURCE_TRANSFORM_ABI, offsetof(struct k_cordis_source_transform_v1_api, last_error),
        start, stop, transform, NULL
    };
    return &api;
}
