#include "cmath2.h"
#include <stddef.h>

_Static_assert(offsetof(cmath2_ints, len) == sizeof(void *), "K slices are packed");
_Static_assert(sizeof(cmath2_ints) == sizeof(void *) + sizeof(int64_t), "K slice size");
#ifdef __wasi__
_Static_assert(sizeof(void *) == 4, "the WASI fixture uses 32-bit pointers");
#endif

int32_t cmath2_square(int32_t x) { return x * x; }

int64_t cmath2_dot(cmath2_ints a, cmath2_ints b) {
    int64_t n = a.len < b.len ? a.len : b.len, sum = 0;
    for (int64_t i = 0; i < n; i++) sum += (int64_t)a.data[i] * b.data[i];
    return sum;
}
