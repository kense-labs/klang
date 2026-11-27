/* The same runtime object AOT programs link, hosted by the compiler. The JIT
   keeps its generated types, stack pointers and inline helpers in its own
   unit; platform operations and their headers are compiled here. */
#include "jit_runtime.h"
#include <setjmp.h>
#include <stdlib.h>

static jmp_buf k_jit_jump;
static int k_jit_status;
static const char *k_jit_resource_dir;
static void k_jit_exit(int code);

#define K_SEPARATE_RUNTIME 1
#define K_RUNTIME_OBJECT 1
#define K_NEED_THREADS 1
#define K_HOSTED_JIT 1
#define K_JIT_RESOURCE_DIR k_jit_resource_dir
#include "runtime/runtime.h"
#include "runtime/runtime_impl.h"
#include "runtime/runtime_threads.h"
#include "runtime/runtime_ext.h"
#include "runtime/runtime_os.h"

static void k_jit_exit(int code) {
    k_jit_status = code;
    longjmp(k_jit_jump, 1);
}

#define K_JIT_SYMBOL(f) { #f, (const void *)f },
static const KlangJitSymbol k_core_jit_symbols[] = {
#include "jit_symbols.inc"
    K_JIT_SYMBOL(memcpy)
    K_JIT_SYMBOL(memmove)
    K_JIT_SYMBOL(memset)
    K_JIT_SYMBOL(memcmp)
    K_JIT_SYMBOL(memchr)
    K_JIT_SYMBOL(strlen)
    K_JIT_SYMBOL(malloc)
    K_JIT_SYMBOL(calloc)
    K_JIT_SYMBOL(realloc)
    K_JIT_SYMBOL(qsort)
    K_JIT_SYMBOL(free)
    K_JIT_SYMBOL(abs)
#define K_JIT_MATH(fn) K_JIT_SYMBOL(fn) K_JIT_SYMBOL(fn##f)
    K_JIT_MATH(sqrt)
    K_JIT_MATH(sin)
    K_JIT_MATH(cos)
    K_JIT_MATH(tan)
    K_JIT_MATH(asin)
    K_JIT_MATH(acos)
    K_JIT_MATH(atan)
    K_JIT_MATH(atan2)
    K_JIT_MATH(exp)
    K_JIT_MATH(log)
    K_JIT_MATH(log2)
    K_JIT_MATH(log10)
    K_JIT_MATH(pow)
    K_JIT_MATH(floor)
    K_JIT_MATH(ceil)
    K_JIT_MATH(round)
    K_JIT_MATH(trunc)
    K_JIT_MATH(fabs)
    K_JIT_MATH(fmod)
    K_JIT_MATH(cbrt)
    K_JIT_MATH(hypot)
    K_JIT_MATH(exp2)
    K_JIT_MATH(expm1)
    K_JIT_MATH(log1p)
    K_JIT_MATH(sinh)
    K_JIT_MATH(cosh)
    K_JIT_MATH(tanh)
    K_JIT_MATH(copysign)
    K_JIT_MATH(ldexp)
    K_JIT_MATH(frexp)
    K_JIT_MATH(acosh)
    K_JIT_MATH(asinh)
    K_JIT_MATH(atanh)
    K_JIT_MATH(erf)
    K_JIT_MATH(erfc)
    K_JIT_MATH(tgamma)
    K_JIT_MATH(lgamma)
    K_JIT_MATH(logb)
    K_JIT_MATH(rint)
    K_JIT_MATH(nearbyint)
    K_JIT_MATH(fmin)
    K_JIT_MATH(fmax)
    K_JIT_MATH(fdim)
    K_JIT_MATH(nextafter)
    K_JIT_MATH(remainder)
    K_JIT_MATH(fma)
    K_JIT_MATH(scalbn)
    K_JIT_MATH(scalbln)
    K_JIT_MATH(ilogb)
    K_JIT_MATH(lrint)
    K_JIT_MATH(llrint)
    K_JIT_MATH(lround)
    K_JIT_MATH(llround)
    K_JIT_MATH(modf)
    K_JIT_MATH(remquo)
    K_JIT_MATH(nan)
#undef K_JIT_MATH
    { "exit", (const void *)k_jit_exit },
    { NULL, NULL }
};
#undef K_JIT_SYMBOL

const KlangJitSymbol *k_jit_symbols(void) { return k_core_jit_symbols; }

int k_jit_run(int (*entry)(int, char **), void (*cleanup)(void),
                  int argc, char **argv, const char *resource_dir) {
    k_jit_resource_dir = resource_dir;
    k_thread_cap = INT64_MAX;
    k_jit_status = 0;
    if (!setjmp(k_jit_jump)) k_jit_status = entry(argc, argv);
    fflush(NULL);
    /* Main's regions and fault handling, then the program's stack block.
       Workers the program left running are the process's to end, as a
       built program's are (spec 11.2). */
    k_rt_release();
    cleanup();
    k_jit_resource_dir = NULL;
    return k_jit_status;
}
