#ifndef K_CORDIS_TCP_POLICY_H
#define K_CORDIS_TCP_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_CORDIS_TCP_POLICY_ABI 1
#define K_CORDIS_TCP_POLICY_ENTRY "k_cordis_tcp_policy_v1"

/* Optional tcp.policy dependency of a hosted tcp.stream provider. The host
   calls max_capacity before transferring a socket. The policy receives no
   socket or reader. Sizes are measured in TCP stream queue slots. Zero
   allows no streams. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*start)(void **state);
    void (*stop)(void *state);
    uint32_t (*max_capacity)(void *state);
} k_cordis_tcp_policy_api;

#if defined(_WIN32) && defined(K_CORDIS_TCP_POLICY_BUILD)
__declspec(dllexport)
#endif
const k_cordis_tcp_policy_api *k_cordis_tcp_policy_v1(void);

#ifdef __cplusplus
}
#endif
#endif
