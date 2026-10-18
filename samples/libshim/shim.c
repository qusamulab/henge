#define _GNU_SOURCE /* pipe2 */
/* Exercises the pthread / errno / fd / liblog / ALooper / AConfiguration shims from Android
 * code. Each t_* returns 0 on success, else the number of the first failing step. */
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STEP(n, cond) do { if (!(cond)) return (n); } while (0)

/* A bionic-sized mutex/cond followed by a canary: a Darwin object (64 bytes) written in place
 * would trample it. Zero-initialized statics rely on lazy creation. */
static struct {
    pthread_mutex_t m;
    uint64_t canary1;
    pthread_cond_t c;
    uint64_t canary2;
    int ready;
} g = {PTHREAD_MUTEX_INITIALIZER, 0x1122334455667788ull, PTHREAD_COND_INITIALIZER, 0x99aabbccddeeff00ull, 0};

int t_mutex(void) {
    pthread_mutex_t rec;
    pthread_mutexattr_t attr;
    STEP(1, pthread_mutex_lock(&g.m) == 0);
    STEP(2, pthread_mutex_trylock(&g.m) == EBUSY);
    STEP(3, pthread_mutex_unlock(&g.m) == 0);
    STEP(4, pthread_mutex_trylock(&g.m) == 0 && pthread_mutex_unlock(&g.m) == 0);
    pthread_mutexattr_init(&attr);
    STEP(5, pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0);
    STEP(6, pthread_mutex_init(&rec, &attr) == 0);
    STEP(7, pthread_mutex_lock(&rec) == 0 && pthread_mutex_lock(&rec) == 0);
    STEP(8, pthread_mutex_unlock(&rec) == 0 && pthread_mutex_unlock(&rec) == 0);
    STEP(9, pthread_mutex_destroy(&rec) == 0);
    pthread_mutexattr_destroy(&attr);
    STEP(10, g.canary1 == 0x1122334455667788ull);
    return 0;
}

static void *worker(void *arg) {
    pthread_mutex_lock(&g.m);
    g.ready = (int)(intptr_t)arg;
    pthread_cond_signal(&g.c);
    pthread_mutex_unlock(&g.m);
    return (void *)(intptr_t)(g.ready + 1);
}

int t_thread_cond(void) {
    pthread_t t;
    pthread_attr_t attr;
    void *ret = 0;
    STEP(1, pthread_attr_init(&attr) == 0);
    size_t stack = 0;
    STEP(2, pthread_attr_getstacksize(&attr, &stack) == 0 && stack >= (1u << 20));
    g.ready = 0;
    pthread_mutex_lock(&g.m);
    STEP(3, pthread_create(&t, &attr, worker, (void *)41) == 0);
    while (!g.ready) STEP(4, pthread_cond_wait(&g.c, &g.m) == 0);
    pthread_mutex_unlock(&g.m);
    STEP(5, pthread_join(t, &ret) == 0);
    STEP(6, (intptr_t)ret == 42);
    pthread_attr_destroy(&attr);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 20 * 1000 * 1000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&g.m);
    STEP(7, pthread_cond_timedwait(&g.c, &g.m, &ts) == ETIMEDOUT); /* 110 on Linux */
    pthread_mutex_unlock(&g.m);
    STEP(8, g.canary1 == 0x1122334455667788ull && g.canary2 == 0x99aabbccddeeff00ull);
    STEP(9, pthread_create(&t, NULL, worker, (void *)1) == 0 && pthread_join(t, NULL) == 0);
    return 0;
}

int t_fd_errno(void) {
    int fds[2];
    char buf[8] = {0};
    STEP(1, pipe(fds) == 0);
    STEP(2, write(fds[1], "hi", 2) == 2);
    STEP(3, read(fds[0], buf, sizeof buf) == 2 && buf[0] == 'h' && buf[1] == 'i');
    STEP(4, close(fds[0]) == 0 && close(fds[1]) == 0);
    STEP(5, read(fds[0], buf, 1) == -1 && errno == EBADF);
    STEP(6, pipe2(fds, O_NONBLOCK) == 0);
    STEP(7, read(fds[0], buf, 1) == -1 && errno == EAGAIN); /* 11 on Linux, 35 on Darwin */
    STEP(8, strlen(strerror(EAGAIN)) > 0);
    close(fds[0]);
    close(fds[1]);
    return 0;
}

static int g_cb_hits;
static int on_fd(int fd, int events, void *data) {
    char b[8];
    (void)events;
    g_cb_hits += *(int *)data;
    read(fd, b, sizeof b);
    return 1;
}

static void *waker(void *l) {
    usleep(20000);
    ALooper_wake((ALooper *)l);
    return 0;
}

int t_looper(void) {
    int fds[2], fd = -1, ev = 0;
    void *data = 0;
    ALooper *l = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    STEP(1, l != 0 && ALooper_forThread() == l);
    STEP(2, ALooper_pollOnce(0, &fd, &ev, &data) == ALOOPER_POLL_TIMEOUT);
    STEP(3, pipe(fds) == 0);
    STEP(4, ALooper_addFd(l, fds[0], 7, ALOOPER_EVENT_INPUT, 0, (void *)0x55) == 1);
    STEP(5, ALooper_pollOnce(0, &fd, &ev, &data) == ALOOPER_POLL_TIMEOUT);
    write(fds[1], "x", 1);
    STEP(6, ALooper_pollOnce(100, &fd, &ev, &data) == 7);
    STEP(7, fd == fds[0] && (ev & ALOOPER_EVENT_INPUT) && data == (void *)0x55);
    char b;
    read(fds[0], &b, 1);
    STEP(8, ALooper_removeFd(l, fds[0]) == 1 && ALooper_removeFd(l, fds[0]) == 0);

    int weight = 3;
    STEP(9, ALooper_addFd(l, fds[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, on_fd, &weight) == 1);
    write(fds[1], "y", 1);
    STEP(10, ALooper_pollOnce(100, 0, 0, 0) == ALOOPER_POLL_CALLBACK && g_cb_hits == 3);
    ALooper_removeFd(l, fds[0]);

    pthread_t t;
    pthread_create(&t, 0, waker, l);
    STEP(11, ALooper_pollOnce(2000, 0, 0, 0) == ALOOPER_POLL_WAKE);
    pthread_join(t, 0);
    close(fds[0]);
    close(fds[1]);
    return 0;
}

int t_config(void) {
    char lang[2], country[2];
    AConfiguration *c = AConfiguration_new();
    STEP(1, c != 0);
    AConfiguration_getLanguage(c, lang);
    AConfiguration_getCountry(c, country);
    STEP(2, lang[0] == 'e' && lang[1] == 'n' && country[0] == 'U' && country[1] == 'S');
    STEP(3, AConfiguration_getOrientation(c) == ACONFIGURATION_ORIENTATION_PORT);
    STEP(4, AConfiguration_getTouchscreen(c) == ACONFIGURATION_TOUCHSCREEN_FINGER);
    STEP(5, AConfiguration_getSdkVersion(c) >= 28);
    AConfiguration_delete(c);
    return 0;
}

int t_log(void) {
    STEP(1, __android_log_print(ANDROID_LOG_INFO, "henge-test", "int %d str %s hex %x", 42, "ok", 255) > 0);
    STEP(2, __android_log_write(ANDROID_LOG_WARN, "henge-test", "plain") > 0);
    return 0;
}
