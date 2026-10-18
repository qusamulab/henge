/* bionic compatibility shim: pthread objects, errno and fd I/O used by NativeActivity glue.
 *
 * bionic's pthread_mutex_t (40 bytes), pthread_cond_t (48) and pthread_attr_t (56) are smaller
 * than Darwin's, and Android code embeds them in its own structs, so Darwin objects can't live
 * inside them. Instead the first word holds a pointer to a heap-allocated Darwin object, created
 * lazily on first use (research/abi-differences.md §3). bionic's static initializers are all
 * small constants (0, or the mutex type in bits 14-15), so a word below 0x10000 means "not
 * created yet". Return codes are Linux errno values.
 */
#define _DARWIN_C_SOURCE
#include "linux.h"
#include "shim.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NOT_CREATED_BELOW 0x10000u
#define BIONIC_MUTEX_NORMAL 0
#define BIONIC_MUTEX_RECURSIVE 1
#define BIONIC_MUTEX_ERRORCHECK 2
#define BIONIC_DETACHED 1
#define STACK_DEFAULT ((size_t)1 << 20) /* bionic default; Darwin's 512 KiB is too small for games */

static int lx(int darwin_err)
{
    return darwin_err ? hr_errno_to_linux(darwin_err) : 0;
}

/* ---- mutex ---- */

static int darwin_mutex_type(int bionic_type)
{
    switch (bionic_type) {
    case BIONIC_MUTEX_RECURSIVE: return PTHREAD_MUTEX_RECURSIVE;
    case BIONIC_MUTEX_ERRORCHECK: return PTHREAD_MUTEX_ERRORCHECK;
    default: return PTHREAD_MUTEX_NORMAL;
    }
}

static pthread_mutex_t *mutex_new(int bionic_type)
{
    pthread_mutex_t *m = calloc(1, sizeof *m);
    pthread_mutexattr_t a;
    if (!m)
        return NULL;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, darwin_mutex_type(bionic_type));
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}

/* The Darwin mutex behind a bionic one, creating it on first use. */
static pthread_mutex_t *mutex_get(uint64_t *slot)
{
    uint64_t v = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (v >= NOT_CREATED_BELOW)
        return (pthread_mutex_t *)v;
    pthread_mutex_t *m = mutex_new((int)((v >> 14) & 3)); /* PTHREAD_*_MUTEX_INITIALIZER carries the type */
    if (!m)
        return NULL;
    if (__atomic_compare_exchange_n(slot, &v, (uint64_t)(uintptr_t)m, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return m;
    pthread_mutex_destroy(m);
    free(m);
    return (pthread_mutex_t *)v; /* lost the race: v is the winner's object */
}

static int shim_mutexattr_init(int *attr)
{
    *attr = BIONIC_MUTEX_NORMAL;
    return 0;
}

static int shim_mutexattr_destroy(int *attr)
{
    (void)attr;
    return 0;
}

static int shim_mutexattr_settype(int *attr, int type)
{
    if (type < BIONIC_MUTEX_NORMAL || type > BIONIC_MUTEX_ERRORCHECK)
        return L_EINVAL;
    *attr = type;
    return 0;
}

static int shim_mutexattr_gettype(const int *attr, int *type)
{
    *type = *attr;
    return 0;
}

static int shim_mutex_init(uint64_t *slot, const int *attr)
{
    pthread_mutex_t *m = mutex_new(attr ? *attr : BIONIC_MUTEX_NORMAL);
    if (!m)
        return L_ENOMEM;
    __atomic_store_n(slot, (uint64_t)(uintptr_t)m, __ATOMIC_RELEASE);
    return 0;
}

static int shim_mutex_destroy(uint64_t *slot)
{
    uint64_t v = __atomic_exchange_n(slot, 0, __ATOMIC_ACQ_REL);
    if (v >= NOT_CREATED_BELOW) {
        pthread_mutex_destroy((pthread_mutex_t *)v);
        free((void *)v);
    }
    return 0;
}

static int shim_mutex_lock(uint64_t *slot)
{
    pthread_mutex_t *m = mutex_get(slot);
    return m ? lx(pthread_mutex_lock(m)) : L_ENOMEM;
}

static int shim_mutex_trylock(uint64_t *slot)
{
    pthread_mutex_t *m = mutex_get(slot);
    return m ? lx(pthread_mutex_trylock(m)) : L_ENOMEM;
}

static int shim_mutex_unlock(uint64_t *slot)
{
    pthread_mutex_t *m = mutex_get(slot);
    return m ? lx(pthread_mutex_unlock(m)) : L_ENOMEM;
}

/* ---- condition variable ---- */

static pthread_cond_t *cond_get(uint64_t *slot)
{
    uint64_t v = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (v >= NOT_CREATED_BELOW)
        return (pthread_cond_t *)v;
    pthread_cond_t *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    pthread_cond_init(c, NULL);
    if (__atomic_compare_exchange_n(slot, &v, (uint64_t)(uintptr_t)c, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return c;
    pthread_cond_destroy(c);
    free(c);
    return (pthread_cond_t *)v;
}

static int shim_cond_init(uint64_t *slot, const void *attr)
{
    (void)attr; /* only pthread_condattr_setclock matters, and Darwin has no equivalent */
    *slot = 0;
    return cond_get(slot) ? 0 : L_ENOMEM;
}

static int shim_cond_destroy(uint64_t *slot)
{
    uint64_t v = __atomic_exchange_n(slot, 0, __ATOMIC_ACQ_REL);
    if (v >= NOT_CREATED_BELOW) {
        pthread_cond_destroy((pthread_cond_t *)v);
        free((void *)v);
    }
    return 0;
}

static int shim_cond_signal(uint64_t *slot)
{
    pthread_cond_t *c = cond_get(slot);
    return c ? lx(pthread_cond_signal(c)) : L_ENOMEM;
}

static int shim_cond_broadcast(uint64_t *slot)
{
    pthread_cond_t *c = cond_get(slot);
    return c ? lx(pthread_cond_broadcast(c)) : L_ENOMEM;
}

static int shim_cond_wait(uint64_t *cslot, uint64_t *mslot)
{
    pthread_cond_t *c = cond_get(cslot);
    pthread_mutex_t *m = mutex_get(mslot);
    return c && m ? lx(pthread_cond_wait(c, m)) : L_ENOMEM;
}

/* abstime is CLOCK_REALTIME, as in bionic's default. */
static int shim_cond_timedwait(uint64_t *cslot, uint64_t *mslot, const struct timespec *abstime)
{
    pthread_cond_t *c = cond_get(cslot);
    pthread_mutex_t *m = mutex_get(mslot);
    return c && m ? lx(pthread_cond_timedwait(c, m, abstime)) : L_ENOMEM;
}

/* ---- thread attributes and threads ---- */

static pthread_attr_t *attr_get(uint64_t *slot)
{
    return *slot >= NOT_CREATED_BELOW ? (pthread_attr_t *)*slot : NULL;
}

static int shim_attr_init(uint64_t *slot)
{
    pthread_attr_t *a = calloc(1, sizeof *a);
    if (!a)
        return L_ENOMEM;
    pthread_attr_init(a);
    pthread_attr_setstacksize(a, STACK_DEFAULT);
    *slot = (uint64_t)(uintptr_t)a;
    return 0;
}

static int shim_attr_destroy(uint64_t *slot)
{
    pthread_attr_t *a = attr_get(slot);
    if (a) {
        pthread_attr_destroy(a);
        free(a);
    }
    *slot = 0;
    return 0;
}

static int shim_attr_setdetachstate(uint64_t *slot, int state)
{
    pthread_attr_t *a = attr_get(slot);
    if (!a)
        return L_EINVAL;
    return lx(pthread_attr_setdetachstate(a, state == BIONIC_DETACHED ? PTHREAD_CREATE_DETACHED
                                                                      : PTHREAD_CREATE_JOINABLE));
}

static int shim_attr_setstacksize(uint64_t *slot, size_t size)
{
    pthread_attr_t *a = attr_get(slot);
    return a ? lx(pthread_attr_setstacksize(a, size)) : L_EINVAL;
}

static int shim_attr_getstacksize(const uint64_t *slot, size_t *size)
{
    const pthread_attr_t *a = *slot >= NOT_CREATED_BELOW ? (const pthread_attr_t *)*slot : NULL;
    return a ? lx(pthread_attr_getstacksize(a, size)) : L_EINVAL;
}

/* bionic's pthread_t is an unsigned long, Darwin's a pointer: same size, same value. */
static int shim_pthread_create(uint64_t *thread, uint64_t *attr, void *(*start)(void *), void *arg)
{
    pthread_attr_t local, *a = attr ? attr_get(attr) : NULL;
    if (!a) {
        pthread_attr_init(&local);
        pthread_attr_setstacksize(&local, STACK_DEFAULT);
        a = &local;
    }
    pthread_t t;
    int e = pthread_create(&t, a, start, arg);
    if (a == &local)
        pthread_attr_destroy(&local);
    if (e)
        return lx(e);
    *thread = (uint64_t)(uintptr_t)t;
    return 0;
}

static uint64_t shim_pthread_self(void)
{
    return (uint64_t)(uintptr_t)pthread_self();
}

static int shim_pthread_join(uint64_t t, void **ret)
{
    return lx(pthread_join((pthread_t)(uintptr_t)t, ret));
}

static int shim_pthread_detach(uint64_t t)
{
    return lx(pthread_detach((pthread_t)(uintptr_t)t));
}

static int shim_pthread_equal(uint64_t a, uint64_t b)
{
    return a == b;
}

/* ---- errno and fd I/O ---- */

/* Code reads errno through __errno(); the shims below store Linux values here. */
static _Thread_local int t_errno;

static int *shim_errno(void)
{
    return &t_errno;
}

static long note_errno(long r)
{
    if (r < 0)
        t_errno = hr_errno_to_linux(errno);
    return r;
}

static long shim_read(int fd, void *buf, size_t n)
{
    return note_errno(read(fd, buf, n));
}

static long shim_write(int fd, const void *buf, size_t n)
{
    return note_errno(write(fd, buf, n));
}

static int shim_close(int fd)
{
    return (int)note_errno(close(fd));
}

static int shim_pipe2(int fds[2], int linux_flags)
{
    if (pipe(fds) != 0)
        return (int)note_errno(-1);
    int flags = hr_oflags_from_linux(linux_flags);
    for (int i = 0; i < 2; i++) {
        if (flags & O_NONBLOCK)
            fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        if (flags & O_CLOEXEC)
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

static int shim_pipe(int fds[2])
{
    return shim_pipe2(fds, 0);
}

/* Linux clock ids differ from Darwin's (linux.h). */
static int shim_clock_gettime(int linux_clock, struct timespec *ts)
{
    int clk = hr_clock_from_linux(linux_clock);
    if (clk < 0) {
        t_errno = L_EINVAL;
        return -1;
    }
    return (int)note_errno(clock_gettime((clockid_t)clk, ts));
}

static int shim_usleep(unsigned usec)
{
    return (int)note_errno(usleep(usec));
}

static const char *shim_strerror(int linux_errno)
{
    return strerror(hr_errno_from_linux(linux_errno));
}

#define E HR_SHIM
const struct hr_shim_entry hr_shim_posix[] = {
    E("pthread_mutexattr_init", shim_mutexattr_init),
    E("pthread_mutexattr_destroy", shim_mutexattr_destroy),
    E("pthread_mutexattr_settype", shim_mutexattr_settype),
    E("pthread_mutexattr_gettype", shim_mutexattr_gettype),
    E("pthread_mutex_init", shim_mutex_init),
    E("pthread_mutex_destroy", shim_mutex_destroy),
    E("pthread_mutex_lock", shim_mutex_lock),
    E("pthread_mutex_trylock", shim_mutex_trylock),
    E("pthread_mutex_unlock", shim_mutex_unlock),
    E("pthread_cond_init", shim_cond_init),
    E("pthread_cond_destroy", shim_cond_destroy),
    E("pthread_cond_signal", shim_cond_signal),
    E("pthread_cond_broadcast", shim_cond_broadcast),
    E("pthread_cond_wait", shim_cond_wait),
    E("pthread_cond_timedwait", shim_cond_timedwait),
    E("pthread_attr_init", shim_attr_init),
    E("pthread_attr_destroy", shim_attr_destroy),
    E("pthread_attr_setdetachstate", shim_attr_setdetachstate),
    E("pthread_attr_setstacksize", shim_attr_setstacksize),
    E("pthread_attr_getstacksize", shim_attr_getstacksize),
    E("pthread_create", shim_pthread_create),
    E("pthread_self", shim_pthread_self),
    E("pthread_join", shim_pthread_join),
    E("pthread_detach", shim_pthread_detach),
    E("pthread_equal", shim_pthread_equal),
    E("__errno", shim_errno),
    E("strerror", shim_strerror),
    E("clock_gettime", shim_clock_gettime),
    E("usleep", shim_usleep),
    E("read", shim_read),
    E("write", shim_write),
    E("close", shim_close),
    E("pipe", shim_pipe),
    E("pipe2", shim_pipe2),
};
#undef E
const size_t hr_shim_posix_count = sizeof hr_shim_posix / sizeof hr_shim_posix[0];
