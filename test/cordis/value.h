#ifndef K_CORDIS_TEST_VALUE_H
#define K_CORDIS_TEST_VALUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct k_cordis_test_value_api {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    int32_t (*read)(void *state);
};

#if defined(_WIN32) && defined(K_CORDIS_TEST_SHARED)
__declspec(dllexport)
#endif
const struct k_cordis_test_value_api *k_cordis_test_value_v1(void);

#ifdef __cplusplus
}
#endif
#endif
