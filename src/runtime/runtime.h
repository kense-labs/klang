/* K runtime — the part every compiler-generated C file starts with.
   Plain C99 (+ #pragma pack, alignment typedefs); compiles with MSVC,
   gcc, clang, and tcc. Kept deliberately small: per-operation behavior (push,
   indexing, field access) is emitted inline by the compiler; only genuinely
   shared machinery lives in the runtime (data stacks, varints, printing,
   aborts, threads/queues, byte search).

   This file holds what a program's own translation unit needs: types,
   macros, the configuration, the helpers that must inline (arithmetic,
   checks, varints), the data stack state the emitted code reads, and
   declarations of the rest. That rest (runtime_impl.h,
   runtime_threads.h, and runtime_os.h after runtime_ext.h) needs the
   platform's headers, which no program's unit includes, and is built one
   of two ways:

   - Standalone (`k --standalone`): one translation
     unit holds it all, and everything the runtime defines is static.
   - Separate (JIT and `k -o`'s default): the program's unit declares the rest
     extern (K_SEPARATE_RUNTIME), and `k --emit-runtime` writes it out
     as a C file of its own (K_RUNTIME_OBJECT), compiled once and linked
     with every program. JIT uses the same interface to the runtime built
     into K. That object takes no configuration of its own: the
     program hands it the data stack sizes as it starts (k_rt_init), it
     always supports threads, and it holds the checks of debug and release
     builds alike, so one object serves every program a compiler emits.
     K_RUNTIME_VERSION names k_rt_start after the runtime's text, so a
     program linked with another compiler's runtime fails to link. */

/* SRW locks, condition variables, and GetTickCount64 require Windows Vista.
   Set the target before any system header can supply an older default. */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#error "K requires Windows Vista or later (_WIN32_WINNT >= 0x0600)"
#endif
#endif

/* The OS layer (runtime_os.h) opens files on Windows with _wfopen, which the
   Microsoft CRT deprecates in favour of its own _s variant. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

/* The POSIX and BSD declarations the runtime uses (sigaltstack, stack_t,
   MAP_ANONYMOUS, ...) are what glibc's headers declare by default, but a
   strict ISO mode (-std=c11, -std=c23) hides them unless this comes before
   the first system header. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#ifndef K_EMBEDDED_JIT
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#endif

/* K values can start at any byte, including scalars reached through a
   reference or slice. GCC and Clang must retain that alignment on the
   pointee type, even after taking a packed member's address. MSVC and
   TinyCC already permit these accesses on the supported targets. The
   aliases keep the underlying C types and their calling conventions. */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define K_ALIGN1 __attribute__((aligned(1)))
#else
#define K_ALIGN1
#endif
typedef int16_t k_i16 K_ALIGN1;
typedef int32_t k_i32 K_ALIGN1;
typedef int64_t k_i64 K_ALIGN1;
typedef uint16_t k_u16 K_ALIGN1;
typedef uint32_t k_u32 K_ALIGN1;
typedef uint64_t k_u64 K_ALIGN1;
typedef float k_f32 K_ALIGN1;
typedef double k_f64 K_ALIGN1;

/* Every float operation rounds on its own. A C compiler may otherwise
   contract a multiply and an add into one fused rounding wherever the target
   has FMA (arm64, and x86-64 once AVX-512 or -march=native turns it on):
   clang within one expression, gcc across statements too. The program would
   then print other digits there than TinyCC and other targets give it. */
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__) && !defined(__TINYC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

/* The function a dynamic extern is called through (§7.10). Its pointer has
   the K declaration's C types, which match the library's in layout but
   not in name (struct Point_g for the library's Point), so clang's
   -fsanitize=function would report a call that the ABI makes sound. */
#if defined(__clang__)
#define K_DL_CALL __attribute__((no_sanitize("function")))
#else
#define K_DL_CALL
#endif

#ifndef K_NEED_THREADS
#define K_NEED_THREADS 0
#endif
#ifndef K_DEBUG
#define K_DEBUG 0          /* 1: overflow, `as` range, and tag checks (§9.3). */
#endif

/* Configuration; all overridable from the compile command line. */
/* The most data stacks the compiler lets one thread program use at once:
   a static count past this is a compile error. The runtime takes the
   counts from the program and never checks this itself. */
#ifndef K_MAX_STACKS
#define K_MAX_STACKS 1024
#endif
#ifndef K_STACK_RESERVE
#define K_STACK_RESERVE (2048ull << 20)  /* Address space reserved per stack. */
#endif
#ifndef K_STACK_GAP
#define K_STACK_GAP (1ull << 20)   /* Unmapped tail so runaway growth aborts. */
#endif
/* The address space the program means to spend on data stack regions over
   every thread program at once, which is what caps hardware_threads()
   (§11.2): the regions it holds, less the main program's, divided by a
   worker's. Not enforced at reservation; what the platform refuses is
   retried smaller (k_reserve_region). */
#ifndef K_STACK_BUDGET
#define K_STACK_BUDGET (32ull << 40)
#endif
#ifndef K_STACK_STATS
#define K_STACK_STATS 0    /* 1: each thread program reports its stack use as it ends. */
#endif
/* §10.4 caps a stack reservation at 2^48 bytes, which is what lets the
   compiler treat every size, count and index as fitting in 48 bits: the
   guard region enforces it, so no growth path needs its own check. */
#if K_STACK_RESERVE > (1ull << 48)
#error "K_STACK_RESERVE exceeds the 2^48 limit (k_spec.md 10.4)"
#endif

#ifdef _MSC_VER
#define K_NORETURN __declspec(noreturn)
#define K_NOINLINE __declspec(noinline)
#else
#define K_NORETURN __attribute__((noreturn))
#define K_NOINLINE __attribute__((noinline))
#endif
/* Inlined at every call: the byte search's helpers (runtime_impl.h), and a
   function K wanted to inline but could not (FnSpec::cinline). */
#if defined(__GNUC__) || defined(__clang__)
#define K_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define K_INLINE __forceinline
#else
#define K_INLINE inline
#endif

#if K_NEED_THREADS
#ifdef _MSC_VER
#define K_TLS __declspec(thread)
#else
#define K_TLS __thread
#endif
#else
#define K_TLS
#endif

/* The linkage of everything the runtime defines outside this file. */
#ifdef K_SEPARATE_RUNTIME
#define K_API extern
#else
#define K_API static
#endif

#define K_CAT_(a, b) a##b
#define K_CAT(a, b) K_CAT_(a, b)
#ifdef K_RUNTIME_VERSION
#define k_rt_start K_CAT(k_rt_start_, K_RUNTIME_VERSION)
#endif

/* ---------------------------------------------------------------------------
   Aborts (§9.3). Not catchable; message + nonzero exit, or in a contained
   worker message + the end of that worker. Compiler-emitted checks carry an
   error id plus a file (a static string in the generated code) and line;
   runtime-internal failures use k_panic, which always exits. The int64_t
   returns let checks sit inside expressions. */

enum {
    K_E_CAPACITY,     /* limited array capacity exceeded */
    K_E_SLICE,        /* slice bounds out of range */
    K_E_RELOFF,       /* relative reference offset overflow */
    K_E_ASSERT,       /* assert failed */
    K_E_CAPRANGE,     /* invalid capacity */
    K_E_RESIZEFILL,   /* resize growth requires a fill value */
    K_E_RESIZENEG,    /* resize to a negative length */
    K_E_POP,          /* pop on empty array */
    K_E_THREADID,     /* thread_wait on an unknown thread id */
    K_E_THREADSELF,   /* a worker cannot wait for itself */
    K_E_TAG,          /* corrupt ADT tag (debug builds only) */
    K_E_ENDIAN,       /* serialization on a big-endian host */
    K_E_SLICELEN,     /* slice pool length negative or beyond any data stack */
    K_E_POOLSLICE,    /* a slice handed to a slice pool is not one of its runs */
    K_E_RELNULL,      /* a non-null optional self-relative target has offset zero */
    K_E_POPN,         /* pop_n of a negative count or more than the length */
    K_E_QBOUND,       /* a queue limit of zero or less */
    K_E_ARRAYLEN,     /* array element count negative or beyond a data stack */
    K_E_ARRAYSLICE,   /* a nonempty slice is not wholly inside the array */
    K_E_SPANTREE,     /* a stdlib slice_pool's nodes and tree are no span tree */
};

/* A place in the program's source: where a standard library function
   called from there reports its failures (codegen.h CallerLoc). */
typedef struct {
    const char *file;
    int line;
} k_loc;

K_API K_NORETURN void k_panic(const char *msg);
K_API K_NORETURN void k_abort(int err, const char *file, int line);
/* The program's own abort(msg) (§9.3): the message is K bytes, not a C
   string, so it goes out with an explicit length. */
K_API K_NORETURN void k_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line);
K_API K_NORETURN void k_runtime_exit(int64_t code);
/* Growth from the data stack whose top is `at` that no region could hold,
   reported as the fault handler reports an overrun into the guard gap. */
K_API K_NORETURN void k_stack_overflow(const void *at);
K_API K_NORETURN void k_idxfail(int64_t i, int64_t n, const char *file, int line);
K_API K_NORETURN void k_divfail(const char *file, int line);
K_API K_NORETURN void k_divovf(const char *file, int line);
/* The debug build's failing overflow and `as` checks. */
K_API K_NORETURN void k_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line);
K_API K_NORETURN void k_ovf_neg(int64_t a, const char *type, const char *file, int line);
K_API K_NORETURN void k_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line);
K_API K_NORETURN void k_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line);
/* f32: d is an f32's value, and takes that type's text form. */
K_API K_NORETURN void k_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line);

/* Bounds check as one unsigned compare; the index operand must be side-effect
   free (the compiler guarantees this at emission). The failing arm's int64_t
   gives the result the type of (i) and an int64_t together, and its call is
   one the C compiler knows does not return, also where k_idxfail is in the
   runtime object. */
#define K_IDX(i, n, f, l) \
    ((uint64_t)(i) < (uint64_t)(n) ? (i) \
                                   : (k_idxfail((int64_t)(i), (n), (f), (l)), (int64_t)0))

/* Statically unreachable spots (e.g. an ADT tag no variant matches): checked
   in debug builds, an optimizer hint in release. */
#if K_DEBUG
#define K_UNREACHABLE(f, l) k_abort(K_E_TAG, (f), (l))
#elif defined(_MSC_VER)
#define K_UNREACHABLE(f, l) __assume(0)
#elif defined(__GNUC__)
#define K_UNREACHABLE(f, l) __builtin_unreachable()
#else
#define K_UNREACHABLE(f, l) ((void)0)
#endif

/* simd functions (§7.12): the compiler writes each one's body once per
   instruction-set level, the baseline under the function's name, and the
   baseline calls the highest version the CPU supports. K_SIMD is the
   highest level built: 0 just the baseline, 1 adds an AVX2 version (with
   FMA, BMI1, BMI2, LZCNT and POPCNT, which the check below asks for one by
   one), 2 an AVX-512 one as well (F, BW, CD, DQ and VL: x86-64-v4). The
   versions are built by clang on x86-64, through its target attribute and
   inline assembly for cpuid; the contraction pragma above keeps their float
   results the baseline's, though both have FMA: only math's mul_add uses
   it (K_MUL_ADD_MODE, below). Elsewhere the versions are left out, and so
   is the choice. -DK_SIMD=0 or 1 lowers the level; nothing raises it. */
/* Whole-vector elementwise arithmetic (spec §6.1). Where the C compiler has
   GCC's vector extensions, an elementwise operation on a struct or fixed
   array of 2 to 16 floats of one width, 16, 32 or 64 bytes with no padding,
   is one operation on a vector type of the same layout. Lane by lane it
   computes what the members do. Elsewhere (MSVC, TinyCC, or -DK_VEC=0) the
   operation stays member by member.

   The vector types are aligned(1), since such a value may sit at any
   address (structs are packed), and may_alias, since the generated code
   reads and writes the value's memory through them. It uses them only
   inside expressions: K_VLD, K_VST and vector literals, never as a local
   (the generated code gathers declarations at the top of a function,
   outside the K_VEC test) and never as a parameter or result, so code
   built for one instruction set never hands a vector to code built for
   another, which the x86-64 ABI forbids for wide vectors. */
#if !defined(K_VEC) && (defined(__clang__) || defined(__GNUC__)) && !defined(__TINYC__)
#define K_VEC 1
#endif
#if K_VEC
typedef double k_vf64_2 __attribute__((vector_size(16), aligned(1), may_alias));
typedef double k_vf64_4 __attribute__((vector_size(32), aligned(1), may_alias));
typedef double k_vf64_8 __attribute__((vector_size(64), aligned(1), may_alias));
typedef float k_vf32_4 __attribute__((vector_size(16), aligned(1), may_alias));
typedef float k_vf32_8 __attribute__((vector_size(32), aligned(1), may_alias));
typedef float k_vf32_16 __attribute__((vector_size(64), aligned(1), may_alias));
#define K_VLD(V, p) (*(const V *)(const void *)(p))
#define K_VST(V, p, x) ((void)(*(V *)(void *)(p) = (x)))
#else
#undef K_VEC
#define K_VEC 0
#endif

#if defined(__clang__) && (defined(__x86_64__) || defined(_M_X64)) && !defined(__TINYC__)
#ifndef K_SIMD
#define K_SIMD 2
#endif
#else
#undef K_SIMD
#define K_SIMD 0
#endif
#if K_SIMD >= 1
#define K_SIMD_TARGET1 __attribute__((target("avx2,fma,bmi,bmi2,lzcnt,popcnt")))
#define K_SIMD_TARGET2 \
    __attribute__((target("avx512f,avx512bw,avx512cd,avx512dq,avx512vl,avx2,fma,bmi,bmi2,lzcnt,popcnt")))

static inline void k_cpuid(uint32_t leaf, uint32_t r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}

/* The highest level both the CPU and the operating system support (the OS
   has to save the wider registers: XGETBV's XCR0), capped at K_SIMD. */
static inline int k_simd_detect(void) {
    uint32_t r[4], b7, xcr0, xhi;
    k_cpuid(0, r);
    if (r[0] < 7) return 0;
    k_cpuid(1, r);
    /* OSXSAVE, AVX, POPCNT, FMA. */
    if ((r[2] & (1u << 27 | 1u << 28 | 1u << 23 | 1u << 12)) !=
        (1u << 27 | 1u << 28 | 1u << 23 | 1u << 12))
        return 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xhi) : "c"(0));
    (void)xhi;
    if ((xcr0 & 0x6) != 0x6) return 0;                  /* XMM and YMM state. */
    k_cpuid(7, r);
    b7 = r[1];
    if ((b7 & (1u << 5 | 1u << 3 | 1u << 8)) != (1u << 5 | 1u << 3 | 1u << 8))
        return 0;                                       /* AVX2, BMI1, BMI2. */
    k_cpuid(0x80000000u, r);
    if (r[0] < 0x80000001u) return 0;
    k_cpuid(0x80000001u, r);
    if (!(r[2] & (1u << 5))) return 0;                  /* LZCNT. */
    if (K_SIMD < 2 || (xcr0 & 0xe0) != 0xe0) return 1; /* Opmask and ZMM state. */
    /* AVX512F, DQ, CD, BW, VL. */
    if ((b7 & (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31)) !=
        (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31))
        return 1;
    return 2;
}

/* Detected at the first call and kept. Every thread computes the same
   level, so a relaxed atomic is all a race between two first calls needs. */
static inline int k_simd_level(void) {
    static int level = -1;
    int l = __atomic_load_n(&level, __ATOMIC_RELAXED);
    if (l < 0) {
        l = k_simd_detect();
        __atomic_store_n(&level, l, __ATOMIC_RELAXED);
    }
    return l;
}
#endif

/* math's mul_add (runtime_ext.h): x * y + z, rounded once where the code
   is built for FMA and twice where it is not, so its results are the
   implementation's (§7.12). K_MUL_ADD_BASE says which for code built for
   the target as a whole: fused on arm64 and on x86-64 built for AVX2 or
   past it (-march=x86-64-v3, clang-cl's /arch:AVX2), separate everywhere
   else, under TinyCC and under MSVC. MSVC compiles fma() to a call into the
   C library, even with /arch:AVX2, which also keeps a loop around it from
   vectorizing: a matrix multiply ran 2.7x slower fused than separate. A
   simd function's AVX2 and AVX-512 versions have FMA whatever the target,
   so the compiler sets K_MUL_ADD_MODE to fused around them, and back to
   the base after (EmitSimdVersions). */
#if !defined(__TINYC__) && !(defined(_MSC_VER) && !defined(__clang__)) && \
    (defined(__FMA__) || defined(__aarch64__) || defined(_M_ARM64))
#define K_MUL_ADD_BASE fused
#else
#define K_MUL_ADD_BASE separate
#endif
#define K_MUL_ADD_MODE K_MUL_ADD_BASE

/* The generated code's memcpy and memset of zeros. Most move or clear the
   1 to 32 bytes of a field, an element or a reference, a constant size the
   optimizing backends turn into a few moves. TinyCC calls the C library for
   every memcpy and memset, so under it a constant size of up to 32 bytes
   is written out as loads and stores, overlapping where the size is not a
   power of two (which needs the runs not to overlap, as memcpy's do not).
   TinyCC generates only the arm a constant condition selects, so these
   macros compile to the stores alone or to the library call alone; the
   stores name d and s several times, which the compiler's operands allow
   (addresses of locals, fields, elements and data-stack tops). */
#ifdef __TINYC__
#define K_KSZ(n, lo, hi) (__builtin_constant_p(n) && (n) >= (lo) && (n) <= (hi))
#define K_AT(T, p, k) (*(T *)((uint8_t *)(p) + (k)))
#define K_CP(T, d, s, k) (K_AT(T, d, k) = K_AT(const T, s, k))
#define K_MEMCPY(d, s, n) \
    (K_KSZ(n, 8, 8) ? (void)K_CP(uint64_t, d, s, 0) \
     : K_KSZ(n, 4, 4) ? (void)K_CP(uint32_t, d, s, 0) \
     : K_KSZ(n, 1, 1) ? (void)K_CP(uint8_t, d, s, 0) \
     : K_KSZ(n, 2, 2) ? (void)K_CP(uint16_t, d, s, 0) \
     : K_KSZ(n, 3, 3) ? (void)(K_CP(uint16_t, d, s, 0), K_CP(uint8_t, d, s, 2)) \
     : K_KSZ(n, 5, 7) ? (void)(K_CP(uint32_t, d, s, 0), K_CP(uint32_t, d, s, (n) - 4)) \
     : K_KSZ(n, 9, 16) ? (void)(K_CP(uint64_t, d, s, 0), K_CP(uint64_t, d, s, (n) - 8)) \
     : K_KSZ(n, 17, 32) ? (void)(K_CP(uint64_t, d, s, 0), K_CP(uint64_t, d, s, 8), \
                                  K_CP(uint64_t, d, s, (n) - 16), K_CP(uint64_t, d, s, (n) - 8)) \
     : (void)memcpy((d), (s), (n)))
#define K_MEMZERO(d, n) \
    (K_KSZ(n, 8, 8) ? (void)(K_AT(uint64_t, d, 0) = 0) \
     : K_KSZ(n, 4, 4) ? (void)(K_AT(uint32_t, d, 0) = 0) \
     : K_KSZ(n, 1, 1) ? (void)(K_AT(uint8_t, d, 0) = 0) \
     : K_KSZ(n, 2, 2) ? (void)(K_AT(uint16_t, d, 0) = 0) \
     : K_KSZ(n, 3, 3) ? (void)(K_AT(uint16_t, d, 0) = 0, K_AT(uint8_t, d, 2) = 0) \
     : K_KSZ(n, 5, 7) ? (void)(K_AT(uint32_t, d, 0) = 0, K_AT(uint32_t, d, (n) - 4) = 0) \
     : K_KSZ(n, 9, 16) ? (void)(K_AT(uint64_t, d, 0) = 0, K_AT(uint64_t, d, (n) - 8) = 0) \
     : K_KSZ(n, 17, 32) ? (void)(K_AT(uint64_t, d, 0) = 0, K_AT(uint64_t, d, 8) = 0, \
                                  K_AT(uint64_t, d, (n) - 16) = 0, K_AT(uint64_t, d, (n) - 8) = 0) \
     : (void)memset((d), 0, (n)))
#else
#define K_MEMCPY(d, s, n) memcpy((d), (s), (n))
#define K_MEMZERO(d, n) memset((d), 0, (n))
#endif

/* Unaligned loads of 4 and 8 bytes. */
#ifdef __TINYC__
#define k_ld32(p) K_AT(const uint32_t, p, 0)
#define k_ld64(p) K_AT(const uint64_t, p, 0)
#else
static uint32_t k_ld32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t k_ld64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }
#endif

/* The low k bytes of a word set (none for k <= 0, all for k >= 8): which
   bytes of a little-endian load lie within a length. */
static uint64_t k_bytemask(int64_t k) {
    return k >= 8 ? ~(uint64_t)0 : k <= 0 ? 0 : ((uint64_t)1 << (k * 8)) - 1;
}

/* memcpy, memmove and memcmp for a slice's elements. An empty slice's data
   pointer is NULL where the slice was zero-filled (default<T>(), a default
   element), and C leaves all three undefined on a null pointer even for
   zero bytes. A copy of up to 16 bytes loads both overlapping halves before
   it stores them, and one of up to 256 bytes moves 16 at a time, the last 16
   overlapping, where the library's memcpy would take a call and a dispatch
   on the length; as the C library's memcpy, it takes runs that do not
   overlap. */
static void k_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (n <= 16) {
        if (n >= 8) {
            uint64_t a = k_ld64(s), b = k_ld64(s + n - 8);
            K_MEMCPY(d, &a, 8);
            K_MEMCPY(d + n - 8, &b, 8);
        } else if (n >= 4) {
            uint32_t a = k_ld32(s), b = k_ld32(s + n - 4);
            K_MEMCPY(d, &a, 4);
            K_MEMCPY(d + n - 4, &b, 4);
        } else if (n) {
            uint8_t a = s[0], b = s[n >> 1], c = s[n - 1];
            d[0] = a;
            d[n >> 1] = b;
            d[n - 1] = c;
        }
        return;
    }
    if (n <= 256) {
        for (size_t i = 0; i + 16 < n; i += 16) K_MEMCPY(d + i, s + i, 16);
        K_MEMCPY(d + n - 16, s + n - 16, 16);
        return;
    }
    memcpy(dst, src, n);
}

static void k_memmove(void *dst, const void *src, size_t n) {
    if (n) memmove(dst, src, n);
}

/* Whether n bytes at a and b are equal: two overlapping loads per side up
   to 16 bytes, where keys and names mostly are, then 16 bytes a step with
   the last 16 overlapping, up to 256, the library's memcmp beyond. Below
   that a call and memcmp's own dispatch cost more than the compare. The
   longer compares are a function of their own, so that k_memeq stays small
   enough for the C compiler to inline where the length is known. */
static K_NOINLINE int k_memeq_long(const uint8_t *p, const uint8_t *q, size_t n) {
    if (n > 256) return memcmp(p, q, n) == 0;
    for (size_t i = 0; i + 16 < n; i += 16)
        if ((k_ld64(p + i) ^ k_ld64(q + i)) | (k_ld64(p + i + 8) ^ k_ld64(q + i + 8)))
            return 0;
    return ((k_ld64(p + n - 16) ^ k_ld64(q + n - 16)) |
            (k_ld64(p + n - 8) ^ k_ld64(q + n - 8))) == 0;
}

static int k_memeq(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    if (n >= 8 && n <= 16)
        return ((k_ld64(p) ^ k_ld64(q)) | (k_ld64(p + n - 8) ^ k_ld64(q + n - 8))) == 0;
    if (n >= 4 && n < 8)
        return ((k_ld32(p) ^ k_ld32(q)) | (k_ld32(p + n - 4) ^ k_ld32(q + n - 4))) == 0;
    if (n < 4)
        return n == 0 || ((p[0] ^ q[0]) | (p[n >> 1] ^ q[n >> 1]) | (p[n - 1] ^ q[n - 1])) == 0;
    return k_memeq_long(p, q, n);
}

/* ---------------------------------------------------------------------------
   Integer semantics (§6.2): every operation runs at its operands' type. The
   operations compute wide (so wrap is defined in C), truncate back, and — when
   the generated C is compiled with -DK_DEBUG=1 — abort when the wide result
   does not fit the type. Shifts mask their count to the width; division is
   zero-checked always.

   In a release build every operation is a macro whose body is the
   expression the debug function would have returned. An optimizing backend
   inlines either form to the same instruction, but a backend that does not
   inline (libtcc, or any -O0 build) would otherwise pay a call for every
   arithmetic operation in the program — which measured as 13-37% of total
   runtime across the benchmarks.

   The signed types' add, sub, mul and neg also take the file and line of the
   operation, for the debug build's overflow message; the release macros
   drop them unevaluated. */

/* Division and modulo, both builds: the zero check is not optional, `%` is
   Euclidean, and i64.min / -1 aborts. They are macros for the reason the
   other operations are, but each names its operands more than once, so the
   compiler passes side-effect-free ones (as K_IDX's index); a signed
   modulo also takes t, an int64_t of the caller's that holds the C
   remainder while its sign is fixed. Operands convert to the operation's
   type first, as a function's parameters would. Only the debug build's
   narrow signed division, which also checks that the quotient fits (-128 /
   -1 in i8), is a function. */
#define K_DIVU(T, a, b, f, l) \
    ((T)(b) ? (T)((T)(a) / (T)(b)) : (k_divfail((f), (l)), (T)0))
#define K_MODU(T, a, b, f, l) \
    ((T)(b) ? (T)((T)(a) % (T)(b)) : (k_divfail((f), (l)), (T)0))
#define K_DIVS(T, a, b, f, l) \
    ((T)(b) ? (T)((int64_t)(T)(a) / (int64_t)(T)(b)) : (k_divfail((f), (l)), (T)0))
#define K_MODS(T, t, a, b, f, l) \
    ((T)(b) == 0 ? (k_divfail((f), (l)), (T)0) \
     : (T)(((t) = (int64_t)(T)(a) % (int64_t)(T)(b)) < 0 \
               ? (t) + ((T)(b) < 0 ? -(int64_t)(T)(b) : (int64_t)(T)(b)) : (t)))

#define k_div_u8(a, b, f, l)  K_DIVU(uint8_t, a, b, f, l)
#define k_div_u16(a, b, f, l) K_DIVU(uint16_t, a, b, f, l)
#define k_div_u32(a, b, f, l) K_DIVU(uint32_t, a, b, f, l)
#define k_div_u64(a, b, f, l) K_DIVU(uint64_t, a, b, f, l)
#define k_mod_u8(a, b, f, l)  K_MODU(uint8_t, a, b, f, l)
#define k_mod_u16(a, b, f, l) K_MODU(uint16_t, a, b, f, l)
#define k_mod_u32(a, b, f, l) K_MODU(uint32_t, a, b, f, l)
#define k_mod_u64(a, b, f, l) K_MODU(uint64_t, a, b, f, l)
#define k_mod_i8(t, a, b, f, l)  K_MODS(int8_t, t, a, b, f, l)
#define k_mod_i16(t, a, b, f, l) K_MODS(int16_t, t, a, b, f, l)
#define k_mod_i32(t, a, b, f, l) K_MODS(int32_t, t, a, b, f, l)
#define k_div_i64(a, b, f, l) \
    ((int64_t)(b) == 0 ? (k_divfail((f), (l)), (int64_t)0) \
     : (int64_t)(a) == INT64_MIN && (int64_t)(b) == -1 ? (k_divovf((f), (l)), (int64_t)0) \
     : (int64_t)(a) / (int64_t)(b))
/* i64.min % -1 would trap, and is exactly 0 as every x % -1 is; |b| is
   unsigned, so a divisor of i64.min (unrepresentable negated) works. */
#define k_mod_i64(t, a, b, f, l) \
    ((int64_t)(b) == 0 ? (k_divfail((f), (l)), (int64_t)0) \
     : (int64_t)(b) == -1 ? (int64_t)0 \
     : ((t) = (int64_t)(a) % (int64_t)(b)) < 0 \
         ? (int64_t)((uint64_t)(t) + ((int64_t)(b) < 0 ? 0u - (uint64_t)(int64_t)(b) \
                                                       : (uint64_t)(int64_t)(b))) \
         : (t))

#if K_DEBUG
#define K_DIVOPS_S(SFX, T, MIN, MAX) \
static T k_div_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) k_divfail(file, line); \
    int64_t r = (int64_t)a / (int64_t)b; \
    if (r < (MIN) || r > (MAX)) k_ovf(a, "/", b, #SFX, file, line); \
    return (T)r; }
K_DIVOPS_S(i8, int8_t, -128, 127)
K_DIVOPS_S(i16, int16_t, -32768, 32767)
K_DIVOPS_S(i32, int32_t, INT32_MIN, INT32_MAX)
#else
#define k_div_i8(a, b, f, l)  K_DIVS(int8_t, a, b, f, l)
#define k_div_i16(a, b, f, l) K_DIVS(int16_t, a, b, f, l)
#define k_div_i32(a, b, f, l) K_DIVS(int32_t, a, b, f, l)
#endif

#if K_DEBUG

/* Signed narrow types (8/16/32 bits): 64-bit signed math covers every
   intermediate result. */
#define K_INTOPS_S(SFX, T, MIN, MAX, BITS) \
static T k_add_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a + (int64_t)b; \
    if (r < MIN || r > MAX) k_ovf(a, "+", b, #SFX, file, line); \
    return (T)r; } \
static T k_sub_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a - (int64_t)b; \
    if (r < MIN || r > MAX) k_ovf(a, "-", b, #SFX, file, line); \
    return (T)r; } \
static T k_mul_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a * (int64_t)b; \
    if (r < MIN || r > MAX) k_ovf(a, "*", b, #SFX, file, line); \
    return (T)r; } \
static T k_neg_##SFX(T a, const char *file, int line) { \
    int64_t r = -(int64_t)a; \
    if (r < MIN || r > MAX) k_ovf_neg(a, #SFX, file, line); \
    return (T)r; } \
static T k_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T k_shr_##SFX(T a, int64_t n) { \
    return (T)((int64_t)a >> (n & (BITS - 1))); }

/* Unsigned types wrap modulo 2^width by definition (§6.2) in every build:
   plain C unsigned arithmetic, truncated back to the width. */
#define K_INTOPS_U(SFX, T, MAX, BITS) \
static T k_add_##SFX(T a, T b) { return (T)(a + b); } \
static T k_sub_##SFX(T a, T b) { return (T)(a - b); } \
static T k_mul_##SFX(T a, T b) { return (T)((uint64_t)a * (uint64_t)b); } \
static T k_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T k_shr_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a >> (n & (BITS - 1))); }

K_INTOPS_S(i8,  int8_t,  -128, 127, 8)
K_INTOPS_S(i16, int16_t, -32768, 32767, 16)
K_INTOPS_S(i32, int32_t, INT32_MIN, INT32_MAX, 32)
K_INTOPS_U(u8,  uint8_t,  255u, 8)
K_INTOPS_U(u16, uint16_t, 65535u, 16)
K_INTOPS_U(u32, uint32_t, 4294967295u, 32)

/* The 64-bit types detect overflow on the value itself. */
static int64_t k_add_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) k_ovf(a, "+", b, "i64", file, line);
    return r;
}
static int64_t k_sub_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (((a ^ b) & (a ^ r)) < 0) k_ovf(a, "-", b, "i64", file, line);
    return r;
}
static int64_t k_mul_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a && b &&
        ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN) || r / b != a))
        k_ovf(a, "*", b, "i64", file, line);
    return r;
}
static int64_t k_neg_i64(int64_t a, const char *file, int line) {
    if (a == INT64_MIN) k_ovf_neg(a, "i64", file, line);
    return (int64_t)(0u - (uint64_t)a);
}
static int64_t k_shl_i64(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a << (n & 63));
}
static int64_t k_shr_i64(int64_t a, int64_t n) { return a >> (n & 63); }
static uint64_t k_add_u64(uint64_t a, uint64_t b) { return a + b; }
static uint64_t k_sub_u64(uint64_t a, uint64_t b) { return a - b; }
static uint64_t k_mul_u64(uint64_t a, uint64_t b) { return a * b; }
static uint64_t k_shl_u64(uint64_t a, int64_t n) { return a << (n & 63); }
static uint64_t k_shr_u64(uint64_t a, int64_t n) { return a >> (n & 63); }

#else  /* release: each operation is the expression the function returned */

#define k_add_i8(a, b, f, l)  ((int8_t)((int64_t)(a) + (int64_t)(b)))
#define k_sub_i8(a, b, f, l)  ((int8_t)((int64_t)(a) - (int64_t)(b)))
#define k_mul_i8(a, b, f, l)  ((int8_t)((int64_t)(a) * (int64_t)(b)))
#define k_neg_i8(a, f, l)     ((int8_t)(-(int64_t)(a)))
#define k_shl_i8(a, n)  ((int8_t)((uint64_t)(a) << ((n) & 7)))
#define k_shr_i8(a, n)  ((int8_t)((int64_t)(a) >> ((n) & 7)))

#define k_add_i16(a, b, f, l) ((int16_t)((int64_t)(a) + (int64_t)(b)))
#define k_sub_i16(a, b, f, l) ((int16_t)((int64_t)(a) - (int64_t)(b)))
#define k_mul_i16(a, b, f, l) ((int16_t)((int64_t)(a) * (int64_t)(b)))
#define k_neg_i16(a, f, l)    ((int16_t)(-(int64_t)(a)))
#define k_shl_i16(a, n) ((int16_t)((uint64_t)(a) << ((n) & 15)))
#define k_shr_i16(a, n) ((int16_t)((int64_t)(a) >> ((n) & 15)))

#define k_add_i32(a, b, f, l) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define k_sub_i32(a, b, f, l) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define k_mul_i32(a, b, f, l) ((int32_t)((int64_t)(a) * (int64_t)(b)))
#define k_neg_i32(a, f, l)    ((int32_t)(-(int64_t)(a)))
#define k_shl_i32(a, n) ((int32_t)((uint64_t)(a) << ((n) & 31)))
#define k_shr_i32(a, n) ((int32_t)((int64_t)(a) >> ((n) & 31)))

#define k_add_u8(a, b)  ((uint8_t)((a) + (b)))
#define k_sub_u8(a, b)  ((uint8_t)((a) - (b)))
#define k_mul_u8(a, b)  ((uint8_t)((uint64_t)(a) * (uint64_t)(b)))
#define k_shl_u8(a, n)  ((uint8_t)((uint64_t)(a) << ((n) & 7)))
#define k_shr_u8(a, n)  ((uint8_t)((uint64_t)(a) >> ((n) & 7)))

#define k_add_u16(a, b) ((uint16_t)((a) + (b)))
#define k_sub_u16(a, b) ((uint16_t)((a) - (b)))
#define k_mul_u16(a, b) ((uint16_t)((uint64_t)(a) * (uint64_t)(b)))
#define k_shl_u16(a, n) ((uint16_t)((uint64_t)(a) << ((n) & 15)))
#define k_shr_u16(a, n) ((uint16_t)((uint64_t)(a) >> ((n) & 15)))

#define k_add_u32(a, b) ((uint32_t)((a) + (b)))
#define k_sub_u32(a, b) ((uint32_t)((a) - (b)))
#define k_mul_u32(a, b) ((uint32_t)((uint64_t)(a) * (uint64_t)(b)))
#define k_shl_u32(a, n) ((uint32_t)((uint64_t)(a) << ((n) & 31)))
#define k_shr_u32(a, n) ((uint32_t)((uint64_t)(a) >> ((n) & 31)))

#define k_add_i64(a, b, f, l) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define k_sub_i64(a, b, f, l) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define k_mul_i64(a, b, f, l) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))
#define k_neg_i64(a, f, l)    ((int64_t)(0u - (uint64_t)(a)))
#define k_shl_i64(a, n) ((int64_t)((uint64_t)(a) << ((n) & 63)))
#define k_shr_i64(a, n) ((int64_t)((a) >> ((n) & 63)))

#define k_add_u64(a, b) ((uint64_t)((a) + (b)))
#define k_sub_u64(a, b) ((uint64_t)((a) - (b)))
#define k_mul_u64(a, b) ((uint64_t)((a) * (b)))
#define k_shl_u64(a, n) ((uint64_t)((a) << ((n) & 63)))
#define k_shr_u64(a, n) ((uint64_t)((a) >> ((n) & 63)))

#endif  /* K_DEBUG */

/* Unsigned division by a divisor a loop does not change, in libdivide's
   form: computed once before the loop (k_divu_gen), x / d is then the high
   half of a product, adjusted where K_DIVU_ADD is in `more` and shifted
   (k_divu_q). A zero divisor, and a C compiler without 128-bit products,
   get K_DIVU_NONE, and their divisions the plain operator, which reports a
   zero divisor where the division is. */
#define K_DIVU_NONE 255
#define K_DIVU_ADD 64
#define K_DIVU_SHIFT 63
#if defined(__TINYC__)
#define K_HAVE_U128 0
#elif defined(__SIZEOF_INT128__)
#define K_HAVE_U128 1
static uint64_t k_mulhi_u64(uint64_t a, uint64_t b) {
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}
/* (hi * 2^64) / d and its remainder, for hi < d. Clang targeting the
   Microsoft ABI links no 128-bit division routine, so x86-64 divides with
   the instruction itself. */
static uint64_t k_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
#if defined(__x86_64__)
    uint64_t q, r;
    __asm__("divq %[d]" : "=a"(q), "=d"(r) : [d] "r"(d), "a"((uint64_t)0), "d"(hi));
    *rem = r;
    return q;
#else
    unsigned __int128 n = (unsigned __int128)hi << 64;
    *rem = (uint64_t)(n % d);
    return (uint64_t)(n / d);
#endif
}
#elif defined(_MSC_VER) && _MSC_VER >= 1920 && defined(_M_X64)
#include <intrin.h>
#define K_HAVE_U128 1
static uint64_t k_mulhi_u64(uint64_t a, uint64_t b) { return __umulh(a, b); }
static uint64_t k_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
    return _udiv128(hi, 0, d, rem);
}
#else
#define K_HAVE_U128 0
#endif

static uint64_t k_divu_gen(uint64_t d, uint8_t *more) {
#if K_HAVE_U128
    int k = 63;
    uint64_t rem, m;
    if (d == 0) { *more = K_DIVU_NONE; return 0; }
    while (!(d >> k)) k--;
    if (!(d & (d - 1))) { *more = (uint8_t)k; return 0; }
    /* floor(2^(64+k) / d), which fits since d > 2^k; then the smallest
       power that works, or the 65-bit form one past it. */
    m = k_div128_u64((uint64_t)1 << k, d, &rem);
    if (d - rem < ((uint64_t)1 << k)) {
        *more = (uint8_t)k;
    } else {
        uint64_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem) m++;
        *more = (uint8_t)(k | K_DIVU_ADD);
    }
    return m + 1;
#else
    (void)d;
    *more = K_DIVU_NONE;
    return 0;
#endif
}

static uint64_t k_divu_q(uint64_t x, uint64_t magic, uint8_t more) {
#if K_HAVE_U128
    uint64_t q;
    if (!magic) return x >> more;
    q = k_mulhi_u64(magic, x);
    if (more & K_DIVU_ADD) return (((x - q) >> 1) + q) >> (more & K_DIVU_SHIFT);
    return q >> more;
#else
    (void)magic;
    return x >> more;   /* Never called: K_DIVU_NONE takes the operator. */
#endif
}

/* The same for a dividend and divisor that fit in 32 bits, with a 32-bit
   magic: x / d is the high half of a 32x32 product, which a C compiler
   vectorizes where it keeps the 64x64 one scalar, and which needs no
   128-bit arithmetic, so only a zero divisor gets K_DIVU_NONE. */
static uint32_t k_divu32_gen(uint32_t d, uint8_t *more) {
    int k = 31;
    uint64_t n;
    uint32_t m, rem;
    if (d == 0) { *more = K_DIVU_NONE; return 0; }
    while (!(d >> k)) k--;
    if (!(d & (d - 1))) { *more = (uint8_t)k; return 0; }
    n = (uint64_t)1 << (32 + k);
    m = (uint32_t)(n / d);
    rem = (uint32_t)(n % d);
    if (d - rem < ((uint32_t)1 << k)) {
        *more = (uint8_t)k;
    } else {
        uint32_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem) m++;
        *more = (uint8_t)(k | K_DIVU_ADD);
    }
    return m + 1;
}

static uint32_t k_divu32_q(uint32_t x, uint32_t magic, uint8_t more) {
    uint32_t q;
    if (!magic) return x >> more;
    q = (uint32_t)(((uint64_t)magic * x) >> 32);
    if (more & K_DIVU_ADD) return (((x - q) >> 1) + q) >> (more & K_DIVU_SHIFT);
    return q >> more;
}

/* `as!` float-to-int: truncate toward zero, wrap modulo 2^64 (§6.3). Defined
   the same on every platform, unlike a raw C cast of an out-of-range value.
   A value lies in the i64 range exactly when its truncation does, and there
   the C cast truncates by itself (one hardware conversion, where trunc() is
   a libm call on baseline x86-64), so only NaN and the values beyond the
   range take the wrap, out of line. */
static K_NOINLINE int64_t k_f2iwrap_slow(double d) {
    if (d != d) return 0;
    d = fmod(trunc(d), 18446744073709551616.0);
    if (d < 0) d += 18446744073709551616.0;
    return (int64_t)(uint64_t)d;
}
static int64_t k_f2iwrap(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return (int64_t)d;
    return k_f2iwrap_slow(d);
}

/* `as` conversion checks (§6.3): abort in debug builds whenever the
   conversion would change the value, naming the value, the target type and
   the cast's file and line; identity/plain casts in release, which drop all
   three unevaluated. A conversion to a float is a plain C cast in every build
   and has none. */
#if K_DEBUG

static int64_t k_rangechk(int64_t v, int64_t lo, int64_t hi, const char *type,
                           const char *file, int line) {
    if (v < lo || v > hi) k_asfail_i("out of range", v, type, file, line);
    return v;
}
static uint64_t k_rangechk_u(uint64_t v, uint64_t hi, const char *type, const char *file,
                              int line) {
    if (v > hi) k_asfail_u("out of range", v, type, file, line);
    return v;
}
/* To a signed type or a narrower unsigned one, whose range is lo..hi. */
static int64_t k_f2ichk(double d, int f32, int64_t lo, int64_t hi, const char *type,
                         const char *file, int line) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        k_asfail_f("out of range", d, f32, type, file, line);
    int64_t v = (int64_t)d;
    if (v < lo || v > hi) k_asfail_f("out of range", d, f32, type, file, line);
    if ((double)v != d) k_asfail_f("changes the value", d, f32, type, file, line);
    return v;
}
static uint64_t k_f2uchk(double d, int f32, const char *file, int line) {
    if (!(d >= 0 && d < 18446744073709551616.0))
        k_asfail_f("out of range", d, f32, "u64", file, line);
    uint64_t v = (uint64_t)d;
    if ((double)v != d) k_asfail_f("changes the value", d, f32, "u64", file, line);
    return v;
}
#define K_RANGE(v, lo, hi, t, f, l) k_rangechk((v), (lo), (hi), (t), (f), (l))
#define K_RANGE_U(v, hi, t, f, l)   k_rangechk_u((v), (hi), (t), (f), (l))
#define K_F2I(d, s, lo, hi, t, f, l) k_f2ichk((d), (s), (lo), (hi), (t), (f), (l))
#define K_F2U(d, s, f, l)            k_f2uchk((d), (s), (f), (l))

#else

#define K_RANGE(v, lo, hi, t, f, l) (v)
#define K_RANGE_U(v, hi, t, f, l)   (v)
/* Deterministic truncation in release too. */
#define K_F2I(d, s, lo, hi, t, f, l) k_f2iwrap(d)
#define K_F2U(d, s, f, l)            ((uint64_t)k_f2iwrap(d))

#endif

/* ---------------------------------------------------------------------------
   Data stacks (§1.2, Appendix C.1/C.4): large reserved regions, committed on
   use, bump-pointer allocation, watermark restore on scope exit. Emitted code
   holds them per thread program via k_stks (index = the hidden k_sp
   argument plus a per-function constant); globals own dedicated stacks. The
   regions themselves, and what tells a fault in one from a crash, are the
   runtime's (runtime_impl.h). */

typedef struct {
    uint8_t *top;
} k_stack;

/* The program defines what klang.h gives C compiled on its own (k_stack
   here, k_rhdr, k_rref and k_bld_append after the generated types), so
   an --include header including it gets nothing more. */
#define K_H 1

/* Starts the runtime on main's thread: the program's arguments, each
   region's usable reservation and trailing guard gap, the address space
   budgeted for regions over the whole program, and the most regions the
   main program and any one worker hold (the compiler's static counts),
   which size their registries and give hardware_threads() its cap. */
K_API void k_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions);
/* A fresh region, registered to the calling thread program. */
K_API uint8_t *k_reserve_region(k_stack *stack);
/* The calling thread program's stack use, on stderr (K_STACK_STATS):
   `stacks` is how many of its indexed data stacks exist. */
K_API void k_stack_stats(int64_t stacks);
/* Releases every region of the calling thread program, and what else the
   runtime keeps for its thread. */
K_API void k_release_regions(void);
/* Ends the main program, k_rt_start's inverse, for a host that may start
   a program again in this process (k_exit): releases its regions, and
   what handled faults before the runtime started does again. A thread
   program still running is a runtime error. */
K_API void k_rt_stop(void);

/* ---------------------------------------------------------------------------
   Dynamic externs (§7.10): the shared libraries the program's extern blocks
   name, and the functions it calls through them, bound by name as the
   program starts (docs/design/dynamic_externs.md). */

/* A library: the name its block gives and, where the sidecar spells it for
   a platform, that spelling; NULL where the platform derives its own
   (name.dll, libname.dylib, libname.so). */
typedef struct k_dllib {
    const char *name, *onwindows, *onmacos, *onlinux;
} k_dllib;
/* One function: its library's index in the table, the symbol the library
   is asked for, and the pointer the generated code calls through. */
typedef struct k_dlbind {
    int32_t lib;
    const char *sym;
    void **slot;
} k_dlbind;
/* Opens each library once and fills every slot, on the main thread before
   the globals are initialized. A library that does not load, or a symbol it
   does not have, is a runtime error naming both. */
K_API void k_dl_bind(const k_dllib *libs, int64_t nlibs, const k_dlbind *binds,
                       int64_t nbinds);

/* ---------------------------------------------------------------------------
   Threads and typed queues (§11.2), runtime_threads.h. */

K_API int64_t k_hardware_threads(void);
/* Runs run(entry, args) and then release() on a new thread, args a copy of
   argsize bytes, and returns the worker's id. In a contained worker an
   abort ends run early. */
K_API int64_t k_thread_start(void (*run)(void (*)(uint8_t *), uint8_t *),
                               void (*release)(void), void (*entry)(uint8_t *),
                               const void *args, int64_t argsize, int contained);
/* Whether the worker returned rather than aborted, once it has ended. Where
   `msg` is not NULL, the first such wait for a worker that aborted takes its
   message: `msg` and `len` are set to it, which the caller frees with
   k_free_msg, or to NULL and no bytes. */
K_API uint8_t k_thread_wait(int64_t id, uint8_t **msg, int64_t *len,
                               const char *file, int line);
K_API void k_free_msg(uint8_t *msg);

/* One queue per flat element type the program uses (the compiler emits a
   k_queue global per type, which main sets up before anything can use it).
   Values are contiguous byte images: k_qget, k_qwait and k_qpoll return a
   malloc'd node the caller copies from and frees, k_qwait and k_qpoll NULL
   when nothing came. k_qput returns 0, queueing nothing, when the queue is
   at its limit. */
typedef struct k_qnode {
    struct k_qnode *next;
    int64_t size;
    /* Value bytes follow the header. */
} k_qnode;

typedef struct k_qstate *k_queue;
#define K_QUEUE_INIT NULL

K_API void k_qinit(k_queue *q);
K_API uint8_t k_qput(k_queue *q, const void *data, int64_t size);
K_API k_qnode *k_qget(k_queue *q);
K_API k_qnode *k_qwait(k_queue *q, int64_t ms);
K_API k_qnode *k_qpoll(k_queue *q);
K_API void k_qlimit(k_queue *q, int64_t n, const char *file, int line);
/* Frees a queue and the messages left in it, once no thread program can
   use it (k_exit); k_qinit makes it again. */
K_API void k_qfree(k_queue *q);

#ifndef K_RUNTIME_OBJECT

/* The current thread program's stack block: every stack the compiler
   counted for it, reserved as the program starts (k_stack_block).
   k_sp-relative indices resolve through it. */
static K_TLS k_stack *k_stks;
static K_TLS int64_t k_nstks;

/* The current program instance's globals (k_spec.md 11.1), a struct the
   compiler lays out: main's is its one static instance, a worker's a fresh
   copy of the globals its program uses, taken from the spawning instance
   at spawn like the arguments (11.2). No global is shared between program
   instances; the only C statics a program shares are read-only ones. */
static K_TLS void *k_gl;

#define K_STACK(i) (&k_stks[i])

static void k_stack_init(k_stack *s) {
    s->top = k_reserve_region(s);
}

/* The calling thread program's block of n stacks, each with its region:
   main's from k_rt_init, a worker's from its entry thunk. */
static void k_stack_block(int64_t n) {
    k_stks = (k_stack *)calloc((size_t)(n > 0 ? n : 1), sizeof(k_stack));
    if (!k_stks) k_panic("out of memory allocating stack block");
    for (int64_t i = 0; i < n; i++) k_stack_init(&k_stks[i]);
    k_nstks = n;
}

/* The compiler passes the main program's stack count and the most regions
   it and any one worker hold: their stacks plus the dedicated ones of the
   globals and of a worker's arguments. */
static void k_rt_init(int argc, char **argv, int64_t mainstacks, int64_t mainregions,
                       int64_t workerregions) {
    k_rt_start(argc, argv, K_STACK_RESERVE, K_STACK_GAP, K_STACK_BUDGET, mainregions,
                workerregions);
    k_stack_block(mainstacks);
}

/* What a thread program reports as it ends under K_STACK_STATS. */
static void k_rt_stats(void) {
#if K_STACK_STATS
    k_stack_stats(k_nstks);
#endif
}

#if K_NEED_THREADS
/* A worker's thread program: its entry thunk opens the stack block, whose
   count the compiler knows, and allocates the globals instance. */
static void k_thread_run(void (*entry)(uint8_t *), uint8_t *args) {
    entry(args);
    k_rt_stats();
}

/* What the thread program holds once it has returned or aborted; the
   runtime releases the regions after this. */
static void k_thread_release(void) {
    free(k_gl);
    k_gl = NULL;
    free(k_stks);
    k_stks = NULL;
    k_nstks = 0;
}

static int64_t k_thread_spawn(void (*entry)(uint8_t *), const void *args, int64_t argsize,
                               int contained) {
    return k_thread_start(k_thread_run, k_thread_release, entry, args, argsize, contained);
}
#endif

#endif  /* K_RUNTIME_OBJECT */

/* ---------------------------------------------------------------------------
   Slice pools (§5.4, `reusable[]`). A pool's freelist is a tree of its free
   (index, count) spans, kept on a data stack of its own in nodes of
   K_SPAN_NODE bytes (runtime_impl.h). The emitted code grows the element
   region and fills elements; these only keep the spans, and each takes the
   freelist's base, its node count and its stack's top, which stays past the
   last node. Indices and counts are in elements. */

#define K_SPAN_NODE 512

/* Where a run of cnt elements goes in a pool of len elements, taken off the
   freelist: for alloc_slice, and for a slice realloc_slice moves. */
K_API int64_t k_spans_alloc(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                              int64_t cnt);
/* realloc_slice growing the non-empty slice [idx, idx + ol), which does not
   end the array, to cnt elements: where it starts now. The caller copies
   the elements where that moved. */
K_API int64_t k_spans_regrow(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                               int64_t idx, int64_t ol, int64_t cnt);
/* free_slice, and what realloc_slice lets go of: [idx, idx + cnt) back on
   the freelist. */
K_API void k_spans_free(uint8_t *base, int64_t *n, uint8_t **top, int64_t idx, int64_t cnt);

/* ---------------------------------------------------------------------------
   varint (§3.6): ULEB128; struct/payload/offset values additionally zigzag. */

static int64_t k_uleb_read(const uint8_t *p) {
    uint64_t v = 0;
    int shift = 0;
    for (;;) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return (int64_t)v;
        shift += 7;
    }
}

static int64_t k_uleb_size(const uint8_t *p) {
    const uint8_t *q = p;
    while (*q & 0x80) q++;
    return (int64_t)(q - p) + 1;
}

/* An inline array's length prefix is one byte unless the array holds 128
   elements or more, so the two macros below are what the compiler emits for
   it: a load, a test, and the byte. The continuation is a call the caller's
   loop does not contain, and reaching it means there is a large array to
   walk, against which the call costs nothing. Both read the pointer twice,
   which the emitting sites already assume (they form the element address
   from the same text). A varint *value*, by contrast, is whatever the
   program stored, so scalar fields and relative offsets keep the loop above
   inline, where the compilers peel the first byte themselves. */

static K_NOINLINE int64_t k_uleb_read_slow(const uint8_t *p) {
    return k_uleb_read(p);
}

static K_NOINLINE int64_t k_uleb_size_slow(const uint8_t *p) {
    return k_uleb_size(p);
}

#define K_ULEB_READ(p) ((*(p) & 0x80) ? k_uleb_read_slow(p) : (int64_t)*(p))
#define K_ULEB_SIZE(p) ((*(p) & 0x80) ? k_uleb_size_slow(p) : (int64_t)1)

static int64_t k_uleb_write(uint8_t *p, uint64_t v) {
    uint8_t *q = p;
    for (;;) {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) *q++ = b | 0x80; else { *q++ = b; break; }
    }
    return (int64_t)(q - p);
}

static int64_t k_zig_read(const uint8_t *p) {
    uint64_t u = (uint64_t)k_uleb_read(p);
    return (int64_t)((u >> 1) ^ (0u - (u & 1)));
}

static int64_t k_zig_write(uint8_t *p, int64_t v) {
    return k_uleb_write(p, ((uint64_t)v << 1) ^ (uint64_t)(v >> 63));
}

/* ---------------------------------------------------------------------------
   Verified loading (docs/design/serialization.md): what the generated
   k_verify_<T> walkers are built from. The bytes are untrusted until the
   walk finishes, so every read here is bounded by the image end and reports
   a malformed encoding instead of running past it. */

/* The ULEB128 at p, or 0 if it runs past `end`, past ten bytes, or carries
   payload bits above the 64th, or is not shortest. The result is the byte count. */
static int64_t k_uleb_check(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    const uint8_t *q = p;
    for (;;) {
        uint8_t b;
        if (q >= end) return 0;
        b = *q++;
        if (shift > 63 || (shift == 63 && (b & 0x7e))) return 0;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            if (shift && !b) return 0;  /* Redundant high zero group. */
            break;
        }
        shift += 7;
    }
    *out = v;
    return (int64_t)(q - p);
}

/* The same for a signed (zigzag) varint: a value field or a self-relative
   offset of varint width (3.6). */
static int64_t k_zig_check(const uint8_t *p, const uint8_t *end, int64_t *out) {
    uint64_t u = 0;
    int64_t k = k_uleb_check(p, end, &u);
    if (k) *out = (int64_t)((u >> 1) ^ (0u - (u & 1)));
    return k;
}

/* Element starts, one bit per image byte: the framing pass sets them and the
   link pass asks whether an offset is one. Only images of variable-size
   elements need it -- for fixed ones a start is a multiple of the size. The
   bitmap is one data stack's worth of scratch, so the largest image that can
   be verified is eight times a stack's reservation; from_bytes rejects a
   larger one rather than growing into the guard region. */
#define K_BM_SET(bm, i) ((bm)[(uint64_t)(i) >> 3] |= (uint8_t)(1u << ((i) & 7)))
#define K_BM_GET(bm, i) (((bm)[(uint64_t)(i) >> 3] >> ((i) & 7)) & 1)
#define K_BM_MAX ((int64_t)(K_STACK_RESERVE))

/* An image is little-endian by definition (serialization.md 7), which every
   target this compiles for is. The test is a constant to any optimizer; it is
   here so that a big-endian host fails loudly instead of writing bytes that
   only it can read back. */
static int k_is_le(void) {
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}

/* ---------------------------------------------------------------------------
   Text forms (§3.7): the k_fmt_* functions write a value's text at dst and
   return the byte count (at most K_FMT_MAX); print/str/format are built on
   them. A float takes the shortest form that still round-trips. */

#define K_FMT_MAX 32

K_API int64_t k_fmt_i64(uint8_t *dst, int64_t v);
K_API int64_t k_fmt_u64(uint8_t *dst, uint64_t v);
K_API int64_t k_fmt_f64(uint8_t *dst, double v);
K_API int64_t k_fmt_f32(uint8_t *dst, float v);
K_API int64_t k_fmt_bool(uint8_t *dst, int64_t v);
/* A u8 array inside an aggregate: quoted, with the escapes K reads. */
K_API int64_t k_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n);

/* print(...) (§3.7): the whole line, rendered first, in one write. */
K_API void k_out_bytes(const uint8_t *p, int64_t len);

/* Byte search behind std's find_any and find_pair (runtime_impl.h): the
   first i < n with p[i] in the set, or with p[i] in a and p[i + d] in b
   (i + d < n); -1 if there is none. A set is std's ByteSet. */
K_API int64_t k_scan_any(const uint8_t *p, int64_t n, const void *set);
K_API int64_t k_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b);
