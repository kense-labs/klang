#include "cmath2.h"
#include <stddef.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define CMATH2_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define CMATH2_SA_CAT(a, b) a##b
#define CMATH2_SA_NAME(line) CMATH2_SA_CAT(cmath2_sa_, line)
#define CMATH2_STATIC_ASSERT(cond, msg) typedef char CMATH2_SA_NAME(__LINE__)[(cond) ? 1 : -1]
#endif

CMATH2_STATIC_ASSERT(offsetof(cmath2_ints, len) == sizeof(void *), "K slices are packed");
CMATH2_STATIC_ASSERT(sizeof(cmath2_ints) == sizeof(void *) + sizeof(int64_t), "K slice size");
#ifdef __wasi__
CMATH2_STATIC_ASSERT(sizeof(void *) == 4, "the WASI fixture uses 32-bit pointers");
#endif

int32_t cmath2_square(int32_t x) { return x * x; }

int64_t cmath2_dot(cmath2_ints a, cmath2_ints b) {
    int64_t n = a.len < b.len ? a.len : b.len, sum = 0;
    for (int64_t i = 0; i < n; i++) sum += (int64_t)a.data[i] * b.data[i];
    return sum;
}
