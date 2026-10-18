/* bionic compatibility shim: what libc++ and C++ programs pull in on top of the C basics: locales,
 * wide / multibyte characters (UTF-8 only, stateless), pthread keys / once / rwlocks, sysconf,
 * syscall, stdio streams and a few Android-only calls. */
#define _DARWIN_C_SOURCE
#include "internal.h"
#include "linux.h"
#include "shim.h"

#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <xlocale.h>

#define NOT_CREATED_BELOW 0x10000u
#define WEOF_BIONIC 0xffffffffu

static int lx(int darwin_err) { return darwin_err ? hr_errno_to_linux(darwin_err) : 0; }

/* ---- pthread keys, once, rwlocks ---- */

/* bionic's pthread_key_t is an int; Darwin's an unsigned long that stays small. */
static int shim_key_create(int *key, void (*dtor)(void *))
{
    pthread_key_t k;
    int e = pthread_key_create(&k, dtor);
    if (!e) *key = (int)k;
    return lx(e);
}

static int shim_key_delete(int key) { return lx(pthread_key_delete((pthread_key_t)key)); }
static void *shim_getspecific(int key) { return pthread_getspecific((pthread_key_t)key); }
static int shim_setspecific(int key, const void *v) { return lx(pthread_setspecific((pthread_key_t)key, v)); }

/* bionic's pthread_once_t is an int, 0 = not run. */
static int shim_once(int *once, void (*fn)(void))
{
    int expected = 0;
    if (__atomic_compare_exchange_n(once, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        fn();
        __atomic_store_n(once, 2, __ATOMIC_RELEASE);
        return 0;
    }
    while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2)
        sched_yield();
    return 0;
}

static pthread_rwlock_t *rw_get(uint64_t *slot)
{
    uint64_t v = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (v >= NOT_CREATED_BELOW)
        return (pthread_rwlock_t *)v;
    pthread_rwlock_t *l = calloc(1, sizeof *l);
    if (!l)
        return NULL;
    pthread_rwlock_init(l, NULL);
    if (__atomic_compare_exchange_n(slot, &v, (uint64_t)(uintptr_t)l, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return l;
    pthread_rwlock_destroy(l);
    free(l);
    return (pthread_rwlock_t *)v;
}

static int shim_rw_init(uint64_t *slot, const void *attr) { (void)attr; *slot = 0; return rw_get(slot) ? 0 : L_ENOMEM; }

static int shim_rw_destroy(uint64_t *slot)
{
    uint64_t v = __atomic_exchange_n(slot, 0, __ATOMIC_ACQ_REL);
    if (v >= NOT_CREATED_BELOW) {
        pthread_rwlock_destroy((pthread_rwlock_t *)v);
        free((void *)v);
    }
    return 0;
}

#define RW_OP(fn, call) static int fn(uint64_t *slot) { pthread_rwlock_t *l = rw_get(slot); return l ? lx(call(l)) : L_ENOMEM; }
RW_OP(shim_rw_rdlock, pthread_rwlock_rdlock)
RW_OP(shim_rw_wrlock, pthread_rwlock_wrlock)
RW_OP(shim_rw_tryrdlock, pthread_rwlock_tryrdlock)
RW_OP(shim_rw_trywrlock, pthread_rwlock_trywrlock)
RW_OP(shim_rw_unlock, pthread_rwlock_unlock)

/* ---- sysconf, syscall, process info ---- */

static long shim_sysconf(int name)
{
    switch (name) {
    case 0x0000: return 262144;                                  /* _SC_ARG_MAX */
    case 0x0006: return 100;                                     /* _SC_CLK_TCK */
    case 0x000b: return 1024;                                    /* _SC_OPEN_MAX */
    case 0x0027:
    case 0x0028: return 16384;                                   /* _SC_PAGESIZE */
    case 0x0060:
    case 0x0061: return sysconf(_SC_NPROCESSORS_ONLN);           /* _SC_NPROCESSORS_* */
    case 0x0062:
    case 0x0063: {                                               /* _SC_PHYS_PAGES, _SC_AVPHYS_PAGES */
        uint64_t mem = 0;
        size_t len = sizeof mem;
        sysctlbyname("hw.memsize", &mem, &len, NULL, 0);
        return (long)(mem / 16384 / (name == 0x0063 ? 2 : 1));
    }
    default: hr_set_errno(L_EINVAL); return -1;
    }
}

static long shim_syscall(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    long r = hr_syscall(a0, a1, a2, a3, a4, a5, nr);
    if (r < 0) {
        hr_set_errno((int)-r);
        return -1;
    }
    return r;
}

static unsigned long shim_getauxval(unsigned long type)
{
    (void)type;
    return 0; /* no hwcaps: compiler runtimes fall back to the baseline instructions */
}

static int shim_property_get(const char *name, char *value)
{
    (void)name;
    value[0] = 0;
    return 0;
}

static void shim_noop(void) {}
static void shim_set_abort_message(const char *msg) { (void)msg; }

static int shim_strerror_r(int linux_errno, char *buf, size_t len)
{
    int e = strerror_r(hr_errno_from_linux(linux_errno), buf, len);
    return e ? lx(e) : 0;
}

/* ---- locales ---- */

static int darwin_category(int linux_category)
{
    switch (linux_category) {
    case 0: return LC_CTYPE;
    case 1: return LC_NUMERIC;
    case 2: return LC_TIME;
    case 3: return LC_COLLATE;
    case 4: return LC_MONETARY;
    case 5: return LC_MESSAGES;
    default: return LC_ALL;
    }
}

static char *shim_setlocale(int category, const char *name) { return setlocale(darwin_category(category), name); }

/* The category mask is not translated: C++ asks for LC_ALL_MASK, and a partial locale is rare. */
static locale_t shim_newlocale(int mask, const char *name, locale_t base)
{
    (void)mask;
    return newlocale(LC_ALL_MASK, name && *name ? name : "C", base);
}

/* long double is IEEE binary128 in Android's AArch64 ABI and a plain double on Darwin: widen a double
 * into q0 for the strtold family. */
static void dbl_to_quad(double d, uint64_t *lo, uint64_t *hi)
{
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    uint64_t sign = b >> 63, exp = (b >> 52) & 0x7ff, frac = b & 0xfffffffffffffull;
    uint64_t qexp, mhi, mlo;
    if (exp == 0 && frac == 0) {
        qexp = 0, mhi = 0, mlo = 0;
    } else if (exp == 0x7ff) {
        qexp = 0x7fff, mhi = frac >> 4, mlo = frac << 60;
    } else if (exp == 0) { /* subnormal double: normalize */
        int shift = __builtin_clzll(frac) - 11;
        frac = (frac << shift) & 0xfffffffffffffull;
        qexp = 16383 - 1022 - shift;
        mhi = frac >> 4;
        mlo = frac << 60;
    } else {
        qexp = exp - 1023 + 16383;
        mhi = frac >> 4;
        mlo = frac << 60;
    }
    *hi = sign << 63 | qexp << 48 | mhi;
    *lo = mlo;
}

#define RETURN_QUAD(d)                                                    \
    do {                                                                  \
        uint64_t lo_, hi_;                                                \
        dbl_to_quad((d), &lo_, &hi_);                                     \
        __asm__ volatile("fmov d0, %0\n\tmov v0.d[1], %1" ::"r"(lo_), "r"(hi_) : "v0"); \
    } while (0)

static void shim_strtold(const char *s, char **end) { RETURN_QUAD(strtod(s, end)); }
static void shim_strtold_l(const char *s, char **end, locale_t l) { RETURN_QUAD(strtod_l(s, end, l)); }
static void shim_wcstold(const wchar_t *s, wchar_t **end) { RETURN_QUAD(wcstod(s, end)); }

/* ---- wide / multibyte characters: UTF-8, no shift state ---- */

#define MB_ERR ((size_t)-1)
#define MB_INCOMPLETE ((size_t)-2)

static size_t decode(uint32_t *out, const unsigned char *s, size_t n)
{
    if (n == 0)
        return MB_INCOMPLETE;
    unsigned c = s[0];
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!len) { hr_set_errno(84); return MB_ERR; }
    if (n < len)
        return MB_INCOMPLETE;
    uint32_t cp = len == 1 ? c : c & (0xffu >> (len + 1));
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xc0) != 0x80) { hr_set_errno(84); return MB_ERR; }
        cp = cp << 6 | (s[i] & 0x3f);
    }
    if (cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000)) { hr_set_errno(84); return MB_ERR; }
    *out = cp;
    return len;
}

static size_t encode(char *out, uint32_t cp)
{
    if (cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000)) { hr_set_errno(84); return MB_ERR; }
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xc0 | cp >> 6); out[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12); out[1] = (char)(0x80 | (cp >> 6 & 0x3f)); out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | cp >> 18); out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3f)); out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static size_t shim_mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps)
{
    (void)ps;
    if (!s)
        return 0;
    uint32_t cp;
    size_t r = decode(&cp, (const unsigned char *)s, n);
    if (r == MB_ERR || r == MB_INCOMPLETE)
        return r;
    if (pwc) *pwc = (wchar_t)cp;
    return cp ? r : 0;
}

static int shim_mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    if (!s)
        return 0;
    size_t r = shim_mbrtowc(pwc, s, n, NULL);
    return r == MB_ERR || r == MB_INCOMPLETE ? -1 : (int)r;
}

static size_t shim_mbrlen(const char *s, size_t n, void *ps) { return shim_mbrtowc(NULL, s, n, ps); }

static size_t shim_wcrtomb(char *s, wchar_t wc, void *ps)
{
    (void)ps;
    char tmp[4];
    return s ? encode(s, (uint32_t)wc) : encode(tmp, (uint32_t)wc) == MB_ERR ? MB_ERR : 1;
}

static uint32_t shim_btowc(int c) { return c >= 0 && c < 0x80 ? (uint32_t)c : WEOF_BIONIC; }
static int shim_wctob(uint32_t wc) { return wc < 0x80 ? (int)wc : -1; }
static size_t shim_mb_cur_max(void) { return 4; }

static size_t shim_mbsnrtowcs(wchar_t *dst, const char **src, size_t nms, size_t len, void *ps)
{
    (void)ps;
    const unsigned char *p = (const unsigned char *)*src;
    size_t count = 0;
    while (nms > 0 && (!dst || count < len)) {
        uint32_t cp;
        size_t r = decode(&cp, p, nms);
        if (r == MB_ERR)
            return MB_ERR;
        if (r == MB_INCOMPLETE)
            break;
        if (dst) dst[count] = (wchar_t)cp;
        if (cp == 0) {
            if (dst) *src = NULL;
            return count;
        }
        p += r;
        nms -= r;
        count++;
    }
    if (dst) *src = (const char *)p;
    return count;
}

static size_t shim_mbsrtowcs(wchar_t *dst, const char **src, size_t len, void *ps)
{
    return shim_mbsnrtowcs(dst, src, strlen(*src) + 1, len, ps);
}

static size_t shim_wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len, void *ps)
{
    (void)ps;
    const wchar_t *p = *src;
    size_t count = 0;
    while (nwc > 0) {
        char tmp[4];
        size_t r = encode(tmp, (uint32_t)*p);
        if (r == MB_ERR)
            return MB_ERR;
        if (dst && count + r > len)
            break;
        if (dst) memcpy(dst + count, tmp, r);
        if (*p == 0) {
            if (dst) *src = NULL;
            return count;
        }
        count += r;
        p++;
        nwc--;
    }
    if (dst) *src = p;
    return count;
}

/* ---- stdio streams: bionic's stdin / stdout / stderr are &__sF[0..2] ---- */

static unsigned char hr_sF[1024]; /* only the addresses matter */

FILE *hr_file(void *f)
{
    size_t off = (size_t)((unsigned char *)f - hr_sF);
    if (off >= sizeof hr_sF)
        return f; /* a real FILE* from fopen */
    return off < 76 ? stdin : off < 228 ? stdout : stderr; /* tolerant of sizeof(FILE) in 136..152 */
}

#define E HR_SHIM
const struct hr_shim_entry hr_shim_cxx[] = {
    E("pthread_key_create", shim_key_create), E("pthread_key_delete", shim_key_delete),
    E("pthread_getspecific", shim_getspecific), E("pthread_setspecific", shim_setspecific),
    E("pthread_once", shim_once),
    E("pthread_rwlock_init", shim_rw_init), E("pthread_rwlock_destroy", shim_rw_destroy),
    E("pthread_rwlock_rdlock", shim_rw_rdlock), E("pthread_rwlock_wrlock", shim_rw_wrlock),
    E("pthread_rwlock_tryrdlock", shim_rw_tryrdlock), E("pthread_rwlock_trywrlock", shim_rw_trywrlock),
    E("pthread_rwlock_unlock", shim_rw_unlock),
    E("sysconf", shim_sysconf), E("syscall", shim_syscall), E("getauxval", shim_getauxval),
    E("__system_property_get", shim_property_get), E("android_set_abort_message", shim_set_abort_message),
    E("openlog", shim_noop), E("syslog", shim_noop), E("closelog", shim_noop), E("strerror_r", shim_strerror_r),
    E("setlocale", shim_setlocale), E("newlocale", shim_newlocale), E("uselocale", uselocale),
    E("freelocale", freelocale), E("localeconv", localeconv),
    E("strcoll_l", strcoll_l), E("strxfrm_l", strxfrm_l), E("wcscoll_l", wcscoll_l), E("wcsxfrm_l", wcsxfrm_l),
    E("strftime_l", strftime_l), E("strtoll_l", strtoll_l), E("strtoull_l", strtoull_l),
    E("strtold", shim_strtold), E("strtold_l", shim_strtold_l), E("wcstold", shim_wcstold),
    E("iswlower_l", iswlower_l), E("iswupper_l", iswupper_l), E("iswspace_l", iswspace_l),
    E("iswprint_l", iswprint_l), E("iswblank_l", iswblank_l), E("iswcntrl_l", iswcntrl_l),
    E("iswalpha_l", iswalpha_l), E("iswdigit_l", iswdigit_l), E("iswpunct_l", iswpunct_l),
    E("iswxdigit_l", iswxdigit_l), E("towupper_l", towupper_l), E("towlower_l", towlower_l),
    E("wcslen", wcslen), E("wmemchr", wmemchr), E("wmemcmp", wmemcmp), E("wcstol", wcstol),
    E("wcstoul", wcstoul), E("wcstoll", wcstoll), E("wcstoull", wcstoull), E("wcstof", wcstof),
    E("wcstod", wcstod),
    E("mbrtowc", shim_mbrtowc), E("mbtowc", shim_mbtowc), E("mbrlen", shim_mbrlen), E("wcrtomb", shim_wcrtomb),
    E("btowc", shim_btowc), E("wctob", shim_wctob), E("__ctype_get_mb_cur_max", shim_mb_cur_max),
    E("mbsrtowcs", shim_mbsrtowcs), E("mbsnrtowcs", shim_mbsnrtowcs), E("wcsnrtombs", shim_wcsnrtombs),
    E("__sF", hr_sF),
};
#undef E
const size_t hr_shim_cxx_count = sizeof hr_shim_cxx / sizeof hr_shim_cxx[0];
