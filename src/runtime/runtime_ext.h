/* K runtime: extern-fn support, spliced in after the generated type
   declarations, since it is written against them: sl_u8 (a u8 slice: data,
   len) and k_rref (a reference to a resizable: its header and its data
   stack). The runtime object has no generated types, and lays out the ones
   it uses here as codegen does (CodeGen::EmitCoreTypes, CT). An --include
   header follows this and may use what it defines. The generated program
   calls the OS primitives behind stdlib/os.k (spec §7.10), slice_pool's
   span tree (both defined in runtime_os.h) and std's byte search directly
   from `extern "k_..." fn` declarations; no prototype is emitted for a
   function declared here. */

#ifdef K_RUNTIME_OBJECT
#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } k_rhdr;
typedef struct { k_rhdr *hdr; k_stack *stk; } k_rref;
typedef struct { uint8_t *data; int64_t len; } sl_u8;
#pragma pack(pop)
#endif

/* Appends n bytes to the u8[>..] a builder reference points at: the
   resizable's elements top its stack, so the bytes go at the stack top and
   the header's count grows. */
static void k_bld_append(k_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

/* ffi's cstr and bytes (stdlib/ffi.k): the bytes at a pointer a C
   library handed back as a u64, copied into a builder, so nothing foreign
   is a K value without a copy. The pointer is trusted as every extern
   call trusts C; a null one appends nothing and is the false result. */
static uint8_t k_ffi_cstr(uint64_t p, k_rref out) {
    const char *s = (const char *)(uintptr_t)p;
    if (!s) return 0;
    k_bld_append(out, s, (int64_t)strlen(s));
    return 1;
}
static uint8_t k_ffi_bytes(uint64_t p, int64_t n, k_rref out) {
    if (!p) return 0;
    k_bld_append(out, (const void *)(uintptr_t)p, n);
    return 1;
}

/* math's sqrt (stdlib/math.k). K has no errno, and C's sqrt has to
   set it for a negative argument: clang and gcc then guard the square root
   instruction with a test and a library call at every use, and will not
   vectorize a loop around one. A compiler that has a square root without
   errno uses that instead; the value is the same correctly rounded root
   either way, and NaN for a negative argument. */
#if defined(__has_builtin)
#if __has_builtin(__builtin_elementwise_sqrt)
#define K_SQRT_NO_ERRNO 1
#endif
#endif
#ifdef K_SQRT_NO_ERRNO
static double k_sqrt(double x) { return __builtin_elementwise_sqrt(x); }
static float k_sqrtf(float x) { return __builtin_elementwise_sqrt(x); }
#else
static double k_sqrt(double x) { return sqrt(x); }
static float k_sqrtf(float x) { return sqrtf(x); }
#endif

/* binary's float reads and writes (stdlib/binary.k): a value from its
   bits and back, which memcpy keeps exactly, NaN payloads included. */
static float k_f32_from_bits(uint32_t bits) {
    float x;
    memcpy(&x, &bits, sizeof x);
    return x;
}
static double k_f64_from_bits(uint64_t bits) {
    double x;
    memcpy(&x, &bits, sizeof x);
    return x;
}
static uint32_t k_f32_to_bits(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}
static uint64_t k_f64_to_bits(double x) {
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

/* math's mul_add (stdlib/math.k): x * y + z, fused into one rounding
   where the code is built for FMA, as two roundings elsewhere
   (K_MUL_ADD_MODE, runtime.h). A call names the macro, which picks the
   mode where the call is written: a simd function's AVX2 and AVX-512
   versions fuse whatever the target. A function value is resolved at
   compile time, so its calls name the macro too. The function of the same
   name, in the target's mode, is only for C that names it without the
   macro. The fused form is inlined into its caller, so that it is built
   with the caller's FMA. */
static inline double k_mul_add_separate(double x, double y, double z) { return x * y + z; }
static inline float k_mul_addf_separate(float x, float y, float z) { return x * y + z; }
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
static inline __attribute__((always_inline)) double k_mul_add_fused(double x, double y, double z) {
    return __builtin_fma(x, y, z);
}
static inline __attribute__((always_inline)) float k_mul_addf_fused(float x, float y, float z) {
    return __builtin_fmaf(x, y, z);
}
#else
static inline double k_mul_add_fused(double x, double y, double z) { return fma(x, y, z); }
static inline float k_mul_addf_fused(float x, float y, float z) { return fmaf(x, y, z); }
#endif
#define K_MUL_ADD_CAT2(a, b) a##b
#define K_MUL_ADD_CAT(a, b) K_MUL_ADD_CAT2(a, b)
static double k_mul_add(double x, double y, double z) {
    return K_MUL_ADD_CAT(k_mul_add_, K_MUL_ADD_BASE)(x, y, z);
}
static float k_mul_addf(float x, float y, float z) {
    return K_MUL_ADD_CAT(k_mul_addf_, K_MUL_ADD_BASE)(x, y, z);
}
#define k_mul_add(x, y, z) K_MUL_ADD_CAT(k_mul_add_, K_MUL_ADD_MODE)(x, y, z)
#define k_mul_addf(x, y, z) K_MUL_ADD_CAT(k_mul_addf_, K_MUL_ADD_MODE)(x, y, z)

/* math's other libm functions (stdlib/math.k). C has them set errno on a
   domain or range error, so a C compiler takes each call for a write to
   memory: a loop around one loads again after it whatever it had hoisted
   out or kept in registers. K has no errno, so gcc and clang are told
   that each depends on its arguments alone. Under clang each is libm's own
   function declared under a name of its own with an asm label, which clang
   still knows as that libm function where symbols take no prefix. Where
   they do (Apple's leading underscore), clang marks the label as a literal
   symbol name, "\01_pow", and no longer knows it as libm's. gcc knows a
   function only by the name it is declared under, so there, and under
   clang on Apple, each name forwards to libm's, redeclared const: the
   compiler then still combines sin and cos of one value into sincos, turns
   pow(x, 2.0) into x * x and folds constant arguments. Since gcc 16, gcc
   turns pow(x, 2.0) into x * x only without -fmath-errno (the call would
   set errno on overflow), so under gcc pow squares a constant 2 itself;
   that is the only one of gcc's folds that errno holds back. Elsewhere the
   name forwards to libm. */
#if defined(__clang__) && !defined(__APPLE__) && !defined(__TINYC__)
#define K_LIBM_SYM2(p, f) #p #f
#define K_LIBM_SYM(p, f) K_LIBM_SYM2(p, f)
#define K_LIBM
#define K_LIBM1(f) __asm__(K_LIBM_SYM(__USER_LABEL_PREFIX__, f)) __attribute__((const));
#define K_LIBM2(f) K_LIBM1(f)
#define K_LIBM_POW(f) K_LIBM1(f)
#elif defined(__GNUC__) && !defined(__TINYC__)
__attribute__((const)) double sin(double), cos(double), tan(double), asin(double), acos(double),
    atan(double), atan2(double, double), exp(double), log(double), log2(double), log10(double),
    pow(double, double), hypot(double, double), cbrt(double), exp2(double), expm1(double),
    log1p(double), sinh(double), cosh(double), tanh(double);
__attribute__((const)) float sinf(float), cosf(float), tanf(float), asinf(float), acosf(float),
    atanf(float), atan2f(float, float), expf(float), logf(float), log2f(float), log10f(float),
    powf(float, float), hypotf(float, float), cbrtf(float), exp2f(float), expm1f(float),
    log1pf(float), sinhf(float), coshf(float), tanhf(float);
#define K_LIBM static inline
#define K_LIBM1(f) { return f(x); }
#define K_LIBM2(f) { return f(x, y); }
#define K_LIBM_POW(f) { return __builtin_constant_p(y) && y == 2 ? x * x : f(x, y); }
#else
#define K_LIBM static
#define K_LIBM1(f) { return f(x); }
#define K_LIBM2(f) { return f(x, y); }
#define K_LIBM_POW(f) K_LIBM2(f)
#endif
K_LIBM double k_sin(double x) K_LIBM1(sin)
K_LIBM double k_cos(double x) K_LIBM1(cos)
K_LIBM double k_tan(double x) K_LIBM1(tan)
K_LIBM double k_asin(double x) K_LIBM1(asin)
K_LIBM double k_acos(double x) K_LIBM1(acos)
K_LIBM double k_atan(double x) K_LIBM1(atan)
K_LIBM double k_atan2(double x, double y) K_LIBM2(atan2)
K_LIBM double k_exp(double x) K_LIBM1(exp)
K_LIBM double k_log(double x) K_LIBM1(log)
K_LIBM double k_log2(double x) K_LIBM1(log2)
K_LIBM double k_log10(double x) K_LIBM1(log10)
K_LIBM double k_pow(double x, double y) K_LIBM_POW(pow)
K_LIBM double k_hypot(double x, double y) K_LIBM2(hypot)
K_LIBM double k_cbrt(double x) K_LIBM1(cbrt)
K_LIBM double k_exp2(double x) K_LIBM1(exp2)
K_LIBM double k_expm1(double x) K_LIBM1(expm1)
K_LIBM double k_log1p(double x) K_LIBM1(log1p)
K_LIBM double k_sinh(double x) K_LIBM1(sinh)
K_LIBM double k_cosh(double x) K_LIBM1(cosh)
K_LIBM double k_tanh(double x) K_LIBM1(tanh)
K_LIBM float k_sinf(float x) K_LIBM1(sinf)
K_LIBM float k_cosf(float x) K_LIBM1(cosf)
K_LIBM float k_tanf(float x) K_LIBM1(tanf)
K_LIBM float k_asinf(float x) K_LIBM1(asinf)
K_LIBM float k_acosf(float x) K_LIBM1(acosf)
K_LIBM float k_atanf(float x) K_LIBM1(atanf)
K_LIBM float k_atan2f(float x, float y) K_LIBM2(atan2f)
K_LIBM float k_expf(float x) K_LIBM1(expf)
K_LIBM float k_logf(float x) K_LIBM1(logf)
K_LIBM float k_log2f(float x) K_LIBM1(log2f)
K_LIBM float k_log10f(float x) K_LIBM1(log10f)
K_LIBM float k_powf(float x, float y) K_LIBM_POW(powf)
K_LIBM float k_hypotf(float x, float y) K_LIBM2(hypotf)
K_LIBM float k_cbrtf(float x) K_LIBM1(cbrtf)
K_LIBM float k_exp2f(float x) K_LIBM1(exp2f)
K_LIBM float k_expm1f(float x) K_LIBM1(expm1f)
K_LIBM float k_log1pf(float x) K_LIBM1(log1pf)
K_LIBM float k_sinhf(float x) K_LIBM1(sinhf)
K_LIBM float k_coshf(float x) K_LIBM1(coshf)
K_LIBM float k_tanhf(float x) K_LIBM1(tanhf)

/* std's find_any and find_pair over a slice (k_scan_any, k_scan_pair); a
   set is a pointer to std's ByteSet. */
static int64_t k_find_any(sl_u8 s, const void *set) { return k_scan_any(s.data, s.len, set); }
static int64_t k_find_pair(sl_u8 s, const void *a, int64_t d, const void *b) {
    return k_scan_pair(s.data, s.len, a, d, b);
}

/* std's popcount, clz and ctz (64 for zero): the instruction where the C
   compiler has one, and counting under TinyCC. MSVC's popcount is an
   instruction past x86-64's baseline, which /arch:AVX and up promise. */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
static int64_t k_popcount(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER) && defined(_M_X64) && defined(__AVX__)
    return (int64_t)__popcnt64(x);
#else
    int64_t n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
#endif
}
static int64_t k_clz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_clzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanReverse64(&i, x) ? 63 - (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x >> 63)) {
        x <<= 1;
        n++;
    }
    return n;
#endif
}
static int64_t k_ctz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_ctzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanForward64(&i, x) ? (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
#endif
}

/* std's rotl, rotr and byte_swap, so each is one instruction rather than an
   idiom the C compiler may or may not recognize: MSVC's intrinsics, GCC's
   and clang's byte-swap builtins, and the rotate idiom both GCC and clang
   turn into rol/ror. Under TinyCC they are plain shifts. The count is taken
   modulo the width, so a negative one turns the other way. */
static uint32_t k_rotl32(uint32_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotl(x, (int)(n & 31));
#else
    uint32_t s = (uint32_t)n & 31;
    return (x << s) | (x >> (-s & 31));
#endif
}
static uint64_t k_rotl64(uint64_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotl64(x, (int)(n & 63));
#else
    uint32_t s = (uint32_t)n & 63;
    return (x << s) | (x >> (-s & 63));
#endif
}
static uint32_t k_rotr32(uint32_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotr(x, (int)(n & 31));
#else
    uint32_t s = (uint32_t)n & 31;
    return (x >> s) | (x << (-s & 31));
#endif
}
static uint64_t k_rotr64(uint64_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotr64(x, (int)(n & 63));
#else
    uint32_t s = (uint32_t)n & 63;
    return (x >> s) | (x << (-s & 63));
#endif
}
static uint16_t k_byte_swap16(uint16_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)
    return _byteswap_ushort(x);
#else
    return (uint16_t)((x << 8) | (x >> 8));
#endif
}
static uint32_t k_byte_swap32(uint32_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return (x << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | (x >> 24);
#endif
}
static uint64_t k_byte_swap64(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((uint64_t)k_byte_swap32((uint32_t)x) << 32) | k_byte_swap32((uint32_t)(x >> 32));
#endif
}

K_API uint8_t k_os_read_file(sl_u8 path, k_rref out);
K_API uint8_t k_os_write_file(sl_u8 path, sl_u8 data);
K_API uint8_t k_os_write_file_atomic(sl_u8 path, sl_u8 data);
K_API uint8_t k_os_append_file(sl_u8 path, sl_u8 data);
K_API uint8_t k_os_file_exists(sl_u8 path);
K_API uint8_t k_os_remove_file(sl_u8 path);
K_API uint8_t k_os_rename_file(sl_u8 from, sl_u8 to);
K_API uint8_t k_os_make_dir(sl_u8 path);
K_API uint8_t k_os_is_dir(sl_u8 path);
K_API uint8_t k_os_remove_dir(sl_u8 path);
K_API uint8_t k_os_list_dir(sl_u8 path, k_rref out);
K_API void k_os_write_stdout(sl_u8 s);
K_API void k_os_write_stderr(sl_u8 s);
K_API void k_os_flush_stdout(void);
K_API uint8_t k_os_read_line(k_rref out);
K_API void k_os_read_stdin(k_rref out);
K_API uint8_t k_os_read_stdin_max(k_rref out, int64_t max);
K_API int64_t k_os_arg_count(void);
K_API void k_os_arg(int64_t i, k_rref out);
K_API uint8_t k_os_getenv(sl_u8 name, k_rref out);
K_API uint8_t k_os_resource_dir(k_rref out);
K_API int64_t k_os_time_ns(void);
K_API int64_t k_os_clock_ns(void);
K_API void k_os_sleep_ms(int64_t ms);
K_API uint64_t k_os_random_u64(void);
K_API uint8_t k_os_random_bytes(sl_u8 out);
/* os.trim_memory: discards unused complete pages of this thread program's
   data stacks. Defined with the region registry it walks (runtime_impl.h),
   but declared here like the other os primitives, so that no prototype is
   emitted for it. For generated code only, whose callers write back their
   cached tops first; C called from K must not call it. 0 means an OS
   request failed; a top outside its region panics. */
K_API uint8_t k_trim_memory(void);
/* The shortest digits of a finite double, by magnitude: the ones print
   writes (runtime_impl.h's k_float_digits), into dig, which holds at least
   17. Their count, and in *point the power of ten they are a fraction of:
   they spell 0.d1 d2 ... * 10^*point. 0 for zero, NaN and the infinities.
   Behind std's float_text::shortest_digits, for json's format_number. */
K_API int64_t k_f64_digits(double x, sl_u8 dig, int64_t *point);
/* std's slice_pool (stdlib/slice_pool_internal.k) keeps its freelist in
   a checked copy of the runtime's span tree (runtime_os.h): `nodes` is a
   node[>..] of K_SPAN_NODE-byte nodes, and `tree` its header, a field
   beside it. A corrupt tree is reported at `at`, where the program called
   slice_pool (SFunction::takesloc). */
K_API int64_t k_slice_pool_place(k_rref nodes, void *tree, int64_t len, int64_t cnt,
                                   const k_loc *at);
K_API void k_slice_pool_free(k_rref nodes, void *tree, int64_t idx, int64_t cnt,
                               const k_loc *at);
K_API int64_t k_slice_pool_regrow(k_rref nodes, void *tree, int64_t len, int64_t idx,
                                    int64_t ol, int64_t cnt, const k_loc *at);
/* slice_pool's trim: discards the complete pages inside every free span of
   `items`, elements of `elem` bytes; 0 means an OS request failed. */
K_API uint8_t k_slice_pool_trim(k_rref nodes, void *tree, sl_u8 items, int64_t elem,
                                  const k_loc *at);
