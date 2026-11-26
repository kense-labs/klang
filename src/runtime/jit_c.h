/* The C surface used by generated code in the embedded JIT. Platform types
   and implementation headers belong to the runtime built into K. The
   integer and pointer types follow TinyCC's target definitions. */
#ifndef K_JIT_C_H
#define K_JIT_C_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef unsigned __INT64_TYPE__ uint64_t;
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#define NULL ((void *)0)
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#define INFINITY (1.0F / 0.0F)
#define NAN (0.0F / 0.0F)

void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
size_t strlen(const char *);
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
int abs(int);

#define K_JIT_MATH1(fn) double fn(double); float fn##f(float);
#define K_JIT_MATH2(fn) double fn(double, double); float fn##f(float, float);
K_JIT_MATH1(sqrt)
K_JIT_MATH1(sin)
K_JIT_MATH1(cos)
K_JIT_MATH1(tan)
K_JIT_MATH1(asin)
K_JIT_MATH1(acos)
K_JIT_MATH1(atan)
K_JIT_MATH1(exp)
K_JIT_MATH1(log)
K_JIT_MATH1(log2)
K_JIT_MATH1(log10)
K_JIT_MATH1(floor)
K_JIT_MATH1(ceil)
K_JIT_MATH1(round)
K_JIT_MATH1(trunc)
K_JIT_MATH1(fabs)
K_JIT_MATH2(atan2)
K_JIT_MATH2(pow)
K_JIT_MATH2(fmod)
K_JIT_MATH1(cbrt)
K_JIT_MATH2(hypot)
K_JIT_MATH1(exp2)
K_JIT_MATH1(expm1)
K_JIT_MATH1(log1p)
K_JIT_MATH1(sinh)
K_JIT_MATH1(cosh)
K_JIT_MATH1(tanh)
K_JIT_MATH2(copysign)
double ldexp(double, int);
float ldexpf(float, int);
double frexp(double, int *);
float frexpf(float, int *);
K_JIT_MATH1(acosh)
K_JIT_MATH1(asinh)
K_JIT_MATH1(atanh)
K_JIT_MATH1(erf)
K_JIT_MATH1(erfc)
K_JIT_MATH1(tgamma)
K_JIT_MATH1(lgamma)
K_JIT_MATH1(logb)
K_JIT_MATH1(rint)
K_JIT_MATH1(nearbyint)
K_JIT_MATH2(fmin)
K_JIT_MATH2(fmax)
K_JIT_MATH2(fdim)
K_JIT_MATH2(nextafter)
K_JIT_MATH2(remainder)
double fma(double, double, double);
float fmaf(float, float, float);
double scalbn(double, int);
float scalbnf(float, int);
double scalbln(double, long);
float scalblnf(float, long);
int ilogb(double);
int ilogbf(float);
long lrint(double);
long lrintf(float);
long long llrint(double);
long long llrintf(float);
long lround(double);
long lroundf(float);
long long llround(double);
long long llroundf(float);
double modf(double, double *);
float modff(float, float *);
double remquo(double, double, int *);
float remquof(float, float, int *);
double nan(const char *);
float nanf(const char *);
#undef K_JIT_MATH1
#undef K_JIT_MATH2
#endif
