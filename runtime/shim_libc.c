/* bionic compatibility shim: libc and libm functions whose ABI matches Darwin's (fixed arity, pointer /
 * integer / floating-point arguments), and the printf / scanf family, which is variadic.
 *
 * AAPCS64 passes variadic arguments like fixed ones: integers in x3.. (after the named ones), doubles
 * in v0..v7, the overflow on the stack. Darwin's va_list instead holds everything in order on the
 * stack. So the variadic shims declare the arguments as fixed parameters (x registers, then d0..d7,
 * then stack slots), and hr_vformat walks the format string to put them back in call order in an
 * array that is a valid Darwin va_list. Limits: no long double, at most 8 doubles in registers plus
 * STACK_SLOTS on the stack.
 */
#define _DARWIN_C_SOURCE
#include "shim.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MAX_SLOTS 40

/* Where the variadic arguments of one call were passed. */
struct hr_vargs {
    const uint64_t *ints;
    size_t nints, ipos;
    const double *fps;
    size_t nfps, fpos;
    const uint64_t *stack;
    size_t nstack, spos;
};

static uint64_t take_int(struct hr_vargs *v)
{
    if (v->ipos < v->nints)
        return v->ints[v->ipos++];
    return v->spos < v->nstack ? v->stack[v->spos++] : 0;
}

static uint64_t take_fp(struct hr_vargs *v)
{
    if (v->fpos < v->nfps) {
        uint64_t bits;
        memcpy(&bits, &v->fps[v->fpos++], sizeof bits);
        return bits;
    }
    return v->spos < v->nstack ? v->stack[v->spos++] : 0;
}

/* Fills slots (a Darwin va_list image) with the arguments the format consumes, in order.
 * scanf_mode: every conversion takes a pointer, so floating-point conversions use integer slots. */
static size_t build_slots(const char *fmt, struct hr_vargs *v, uint64_t *slots, int scanf_mode)
{
    size_t n = 0;
    for (const char *p = fmt; *p && n < MAX_SLOTS - 2; p++) {
        if (*p != '%')
            continue;
        p++;
        if (*p == '%')
            continue;
        if (scanf_mode && *p == '*') { /* assignment suppression: no argument */
            while (*p && !strchr("diouxXeEfFgGaAcspn[", *p)) p++;
            if (*p == '[') while (*p && *p != ']') p++;
            if (!*p) break;
            continue;
        }
        while (*p && strchr("-+ #0'", *p)) p++;
        if (*p == '*') { slots[n++] = take_int(v); p++; } else while (isdigit((unsigned char)*p)) p++;
        if (*p == '.') {
            p++;
            if (*p == '*') { slots[n++] = take_int(v); p++; } else while (isdigit((unsigned char)*p)) p++;
        }
        while (*p && strchr("hlLzjtq", *p)) p++;
        if (!*p)
            break;
        if (scanf_mode && *p == '[') { /* %[...]: skip the set */
            p++;
            if (*p == '^') p++;
            if (*p == ']') p++;
            while (*p && *p != ']') p++;
            slots[n++] = take_int(v);
            if (!*p) break;
            continue;
        }
        if (!scanf_mode && strchr("eEfFgGaA", *p))
            slots[n++] = take_fp(v);
        else if (strchr("diouxXcspneEfFgGaA", *p))
            slots[n++] = take_int(v);
    }
    return n;
}

#define STACK_SLOTS 12
#define FP_REGS d0, d1, d2, d3, d4, d5, d6, d7
#define FP_PARAMS double d0, double d1, double d2, double d3, double d4, double d5, double d6, double d7
#define STACK_PARAMS uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3, uint64_t s4, uint64_t s5, uint64_t s6, \
                     uint64_t s7, uint64_t s8, uint64_t s9, uint64_t s10, uint64_t s11
#define STACK_ARGS s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11

/* Formats into buf (size n; n == 0 means "unbounded", as sprintf). */
int hr_format(char *buf, size_t n, const char *fmt, const uint64_t *ints, size_t nints, const double *fps,
              const uint64_t *stack)
{
    struct hr_vargs v = {ints, nints, 0, fps, 8, 0, stack, STACK_SLOTS, 0};
    uint64_t slots[MAX_SLOTS] = {0};
    build_slots(fmt, &v, slots, 0);
    va_list ap = (va_list)slots;
    return n ? vsnprintf(buf, n, fmt, ap) : vsprintf(buf, fmt, ap);
}

static int shim_snprintf(char *buf, size_t n, const char *fmt, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                         uint64_t a7, FP_PARAMS, STACK_PARAMS)
{
    const uint64_t ints[] = {a3, a4, a5, a6, a7}, stack[] = {STACK_ARGS};
    const double fps[] = {FP_REGS};
    return hr_format(buf, n ? n : 1, fmt, ints, 5, fps, stack);
}

static int shim_sprintf(char *buf, const char *fmt, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                        uint64_t a7, FP_PARAMS, STACK_PARAMS)
{
    const uint64_t ints[] = {a2, a3, a4, a5, a6, a7}, stack[] = {STACK_ARGS};
    const double fps[] = {FP_REGS};
    return hr_format(buf, 0, fmt, ints, 6, fps, stack);
}

static int shim_printf(const char *fmt, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                       uint64_t a7, FP_PARAMS, STACK_PARAMS)
{
    const uint64_t ints[] = {a1, a2, a3, a4, a5, a6, a7}, stack[] = {STACK_ARGS};
    const double fps[] = {FP_REGS};
    char buf[2048];
    int n = hr_format(buf, sizeof buf, fmt, ints, 7, fps, stack);
    fputs(buf, stdout);
    return n;
}

static int shim_sscanf(const char *str, const char *fmt, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                       uint64_t a6, uint64_t a7, STACK_PARAMS)
{
    const uint64_t ints[] = {a2, a3, a4, a5, a6, a7}, stack[] = {STACK_ARGS};
    struct hr_vargs v = {ints, 6, 0, NULL, 0, 0, stack, STACK_SLOTS, 0};
    uint64_t slots[MAX_SLOTS] = {0};
    build_slots(fmt, &v, slots, 1);
    va_list ap = (va_list)slots;
    return vsscanf(str, fmt, ap);
}

/* __android_log_print lives in shim_android.c and formats through hr_format. */

#define E HR_SHIM
const struct hr_shim_entry hr_shim_libc[] = {
    E("snprintf", shim_snprintf), E("sprintf", shim_sprintf), E("printf", shim_printf), E("sscanf", shim_sscanf),
    /* time, random, strings, conversion */
    E("time", time), E("nanosleep", nanosleep), E("srand", srand), E("rand", rand), E("abs", abs), E("labs", labs),
    E("atoi", atoi), E("atol", atol), E("atof", atof), E("strtol", strtol), E("strtoul", strtoul),
    E("strtod", strtod), E("strtof", strtof), E("strtoll", strtoll), E("strtoull", strtoull),
    E("strchr", strchr), E("strrchr", strrchr), E("strstr", strstr), E("strcpy", strcpy), E("strncpy", strncpy),
    E("strcat", strcat), E("strncat", strncat), E("strncmp", strncmp), E("strcasecmp", strcasecmp),
    E("strncasecmp", strncasecmp), E("strdup", strdup), E("strndup", strndup), E("strtok", strtok),
    E("strtok_r", strtok_r), E("strnlen", strnlen), E("strspn", strspn), E("strcspn", strcspn),
    E("memchr", memchr), E("qsort", qsort), E("bsearch", bsearch), E("getenv", getenv),
    E("posix_memalign", posix_memalign),
    /* ctype */
    E("isalpha", isalpha), E("isdigit", isdigit), E("isalnum", isalnum), E("isspace", isspace),
    E("isupper", isupper), E("islower", islower), E("isxdigit", isxdigit), E("ispunct", ispunct),
    E("isprint", isprint), E("tolower", tolower), E("toupper", toupper),
    /* stdio on files (the FILE* is only ever handed back to these) */
    E("fopen", fopen), E("fclose", fclose), E("fread", fread), E("fwrite", fwrite), E("fseek", fseek),
    E("ftell", ftell), E("fgets", fgets), E("fputs", fputs), E("fflush", fflush), E("rewind", rewind),
    E("puts", puts),
    /* libm */
    E("sinf", sinf), E("cosf", cosf), E("tanf", tanf), E("asinf", asinf), E("acosf", acosf), E("atanf", atanf),
    E("atan2f", atan2f), E("sqrtf", sqrtf), E("powf", powf), E("expf", expf), E("logf", logf),
    E("log10f", log10f), E("log2f", log2f), E("floorf", floorf), E("ceilf", ceilf), E("fabsf", fabsf),
    E("fmodf", fmodf), E("roundf", roundf), E("truncf", truncf), E("fminf", fminf), E("fmaxf", fmaxf),
    E("hypotf", hypotf), E("sinhf", sinhf), E("coshf", coshf), E("tanhf", tanhf),
    E("sin", sin), E("cos", cos), E("tan", tan), E("asin", asin), E("acos", acos), E("atan", atan),
    E("atan2", atan2), E("sqrt", sqrt), E("pow", pow), E("exp", exp), E("log", log), E("log10", log10),
    E("log2", log2), E("floor", floor), E("ceil", ceil), E("fabs", fabs), E("fmod", fmod), E("round", round),
    E("trunc", trunc), E("fmin", fmin), E("fmax", fmax), E("hypot", hypot),
};
#undef E
const size_t hr_shim_libc_count = sizeof hr_shim_libc / sizeof hr_shim_libc[0];
