/* bionic compatibility shim: Android NDK APIs that native_app_glue needs (liblog, ALooper,
 * AInputQueue, AConfiguration). */
#define _DARWIN_C_SOURCE
#include "android.h"
#include "linux.h"
#include "shim.h"

#include <errno.h>
#include <fcntl.h>
#include <os/log.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- liblog ---- */

#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6

static void (*g_log_sink)(int, const char *, const char *, void *);
static void *g_log_sink_user;

void henge_set_log_sink(void (*sink)(int, const char *, const char *, void *), void *user)
{
    g_log_sink_user = user;
    __atomic_store_n(&g_log_sink, sink, __ATOMIC_RELEASE);
}

static void log_emit(int prio, const char *tag, const char *msg)
{
    void (*sink)(int, const char *, const char *, void *) = __atomic_load_n(&g_log_sink, __ATOMIC_ACQUIRE);
    if (sink)
        sink(prio, tag ? tag : "", msg, g_log_sink_user);
    os_log_type_t type = prio >= ANDROID_LOG_ERROR ? OS_LOG_TYPE_ERROR
                         : prio >= ANDROID_LOG_WARN ? OS_LOG_TYPE_DEFAULT
                                                    : OS_LOG_TYPE_INFO;
    os_log_with_type(OS_LOG_DEFAULT, type, "%{public}s: %{public}s", tag ? tag : "", msg);
    fprintf(stderr, "[%s] %s\n", tag ? tag : "", msg);
}

/* __android_log_print is variadic. AAPCS64 passes variadic arguments like fixed ones (integers in
 * x3..x7, then on the stack in 8-byte slots), but Darwin's va_list reads them all from the stack.
 * So declare them as fixed uint64_t parameters (the ones past x7 land on the stack in the same
 * slots) and lay them out as a Darwin va_list. Limits: LOG_MAX_ARGS variadic arguments, none of
 * them floating point (those arrive in v0..v7). Unused slots read harmless stack memory. */
#define LOG_MAX_ARGS 21
static int shim_log_print(int prio, const char *tag, const char *fmt, uint64_t a3, uint64_t a4,
                          uint64_t a5, uint64_t a6, uint64_t a7, uint64_t s0, uint64_t s1, uint64_t s2,
                          uint64_t s3, uint64_t s4, uint64_t s5, uint64_t s6, uint64_t s7, uint64_t s8,
                          uint64_t s9, uint64_t s10, uint64_t s11, uint64_t s12, uint64_t s13, uint64_t s14, uint64_t s15)
{
    uint64_t args[LOG_MAX_ARGS] = {a3, a4, a5, a6, a7, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15};
    char buf[1024];
    va_list ap = (va_list)args;
    vsnprintf(buf, sizeof buf, fmt, ap);
    log_emit(prio, tag, buf);
    return 1;
}

static int shim_log_write(int prio, const char *tag, const char *text)
{
    log_emit(prio, tag, text);
    return 1;
}

/* ---- ALooper ---- */

static _Thread_local struct ALooper *t_looper;

static void set_nonblock(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static struct ALooper *shim_looper_prepare(int opts)
{
    (void)opts;
    if (t_looper)
        return t_looper;
    struct ALooper *l = calloc(1, sizeof *l);
    if (!l)
        return NULL;
    if (pipe(l->wake) != 0) {
        free(l);
        return NULL;
    }
    set_nonblock(l->wake[0]);
    set_nonblock(l->wake[1]);
    l->refs = 1;
    pthread_mutex_init(&l->lock, NULL);
    t_looper = l;
    return l;
}

static struct ALooper *shim_looper_for_thread(void)
{
    return t_looper;
}

static void shim_looper_acquire(struct ALooper *l)
{
    __atomic_add_fetch(&l->refs, 1, __ATOMIC_RELAXED);
}

static void shim_looper_release(struct ALooper *l)
{
    if (__atomic_sub_fetch(&l->refs, 1, __ATOMIC_ACQ_REL) != 0)
        return;
    if (t_looper == l)
        t_looper = NULL;
    close(l->wake[0]);
    close(l->wake[1]);
    pthread_mutex_destroy(&l->lock);
    free(l->fds);
    free(l->pending);
    free(l);
}

static void shim_looper_wake(struct ALooper *l)
{
    char b = 1;
    (void)!write(l->wake[1], &b, 1); /* a full pipe already means "wake pending" */
}

static int shim_looper_add_fd(struct ALooper *l, int fd, int ident, int events, hr_looper_cb cb, void *data)
{
    if (!l || fd < 0 || (!cb && ident < 0))
        return -1;
    pthread_mutex_lock(&l->lock);
    struct hr_looper_fd *slot = NULL;
    for (size_t i = 0; i < l->nfds; i++)
        if (l->fds[i].fd == fd)
            slot = &l->fds[i];
    if (!slot) {
        struct hr_looper_fd *n = realloc(l->fds, (l->nfds + 1) * sizeof *n);
        if (!n) {
            pthread_mutex_unlock(&l->lock);
            return -1;
        }
        l->fds = n;
        slot = &l->fds[l->nfds++];
    }
    *slot = (struct hr_looper_fd){fd, cb ? -2 : ident, events, cb, data};
    pthread_mutex_unlock(&l->lock);
    if (t_looper != l)
        shim_looper_wake(l); /* another thread may be blocked in poll with the old set */
    return 1;
}

static int shim_looper_remove_fd(struct ALooper *l, int fd)
{
    int found = 0;
    if (!l)
        return -1;
    pthread_mutex_lock(&l->lock);
    for (size_t i = 0; i < l->nfds; i++) {
        if (l->fds[i].fd == fd) {
            memmove(&l->fds[i], &l->fds[i + 1], (l->nfds - i - 1) * sizeof *l->fds);
            l->nfds--;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&l->lock);
    return found;
}

static short poll_events(int events)
{
    return (short)(((events & AL_EVENT_INPUT) ? POLLIN : 0) | ((events & AL_EVENT_OUTPUT) ? POLLOUT : 0));
}

static int looper_events(short revents)
{
    return ((revents & POLLIN) ? AL_EVENT_INPUT : 0) | ((revents & POLLOUT) ? AL_EVENT_OUTPUT : 0) |
           ((revents & (POLLERR | POLLNVAL)) ? AL_EVENT_ERROR : 0) | ((revents & POLLHUP) ? AL_EVENT_HANGUP : 0);
}

static int deliver(const struct hr_looper_ready *r, int *out_fd, int *out_events, void **out_data)
{
    if (out_fd) *out_fd = r->fd;
    if (out_events) *out_events = r->events;
    if (out_data) *out_data = r->data;
    return r->ident;
}

static int shim_looper_poll_once(int timeout_ms, int *out_fd, int *out_events, void **out_data)
{
    struct ALooper *l = t_looper;
    if (!l)
        return AL_POLL_ERROR;
    if (out_fd) *out_fd = 0;
    if (out_events) *out_events = 0;
    if (out_data) *out_data = NULL;

    if (l->pending_pos < l->npending)
        return deliver(&l->pending[l->pending_pos++], out_fd, out_events, out_data);
    l->npending = l->pending_pos = 0;

    pthread_mutex_lock(&l->lock);
    size_t n = l->nfds;
    struct hr_looper_fd *snap = malloc((n ? n : 1) * sizeof *snap);
    struct pollfd *pfds = malloc((n + 1) * sizeof *pfds);
    if (!snap || !pfds) {
        pthread_mutex_unlock(&l->lock);
        free(snap);
        free(pfds);
        return AL_POLL_ERROR;
    }
    memcpy(snap, l->fds, n * sizeof *snap);
    pthread_mutex_unlock(&l->lock);

    pfds[0] = (struct pollfd){l->wake[0], POLLIN, 0};
    for (size_t i = 0; i < n; i++)
        pfds[i + 1] = (struct pollfd){snap[i].fd, poll_events(snap[i].events), 0};

    int rc = poll(pfds, (nfds_t)(n + 1), timeout_ms < 0 ? -1 : timeout_ms);
    int result = rc < 0 ? (errno == EINTR ? AL_POLL_WAKE : AL_POLL_ERROR) : rc == 0 ? AL_POLL_TIMEOUT : AL_POLL_WAKE;

    if (rc > 0) {
        if (pfds[0].revents & POLLIN) {
            char drain[64];
            while (read(l->wake[0], drain, sizeof drain) > 0) {}
        }
        for (size_t i = 0; i < n; i++) {
            int ev = looper_events(pfds[i + 1].revents);
            if (!ev)
                continue;
            if (snap[i].cb) {
                if (!snap[i].cb(snap[i].fd, ev, snap[i].data))
                    shim_looper_remove_fd(l, snap[i].fd);
                result = AL_POLL_CALLBACK;
            } else {
                struct hr_looper_ready *p = realloc(l->pending, (l->npending + 1) * sizeof *p);
                if (!p)
                    continue;
                l->pending = p;
                l->pending[l->npending++] = (struct hr_looper_ready){snap[i].fd, snap[i].ident, ev, snap[i].data};
            }
        }
        if (l->pending_pos < l->npending)
            result = deliver(&l->pending[l->pending_pos++], out_fd, out_events, out_data);
    }
    free(snap);
    free(pfds);
    return result;
}

static int shim_looper_poll_all(int timeout_ms, int *out_fd, int *out_events, void **out_data)
{
    int r;
    while ((r = shim_looper_poll_once(timeout_ms, out_fd, out_events, out_data)) == AL_POLL_CALLBACK) {}
    return r;
}

/* ---- AInputQueue ---- */

struct AInputQueue *hr_input_queue_new(void)
{
    struct AInputQueue *q = calloc(1, sizeof *q);
    if (!q)
        return NULL;
    if (pipe(q->pipe) != 0) {
        free(q);
        return NULL;
    }
    return q;
}

void hr_input_queue_free(struct AInputQueue *q)
{
    if (!q)
        return;
    close(q->pipe[0]);
    close(q->pipe[1]);
    free(q);
}

static void shim_input_attach(struct AInputQueue *q, struct ALooper *l, int ident, hr_looper_cb cb, void *data)
{
    q->looper = l;
    shim_looper_add_fd(l, q->pipe[0], ident, AL_EVENT_INPUT, cb, data);
}

static void shim_input_detach(struct AInputQueue *q)
{
    if (q->looper)
        shim_looper_remove_fd(q->looper, q->pipe[0]);
    q->looper = NULL;
}

static int shim_input_has_events(struct AInputQueue *q)
{
    (void)q;
    return 0;
}

static int shim_input_get_event(struct AInputQueue *q, void **event)
{
    (void)q;
    (void)event;
    return -L_EAGAIN; /* nothing queued */
}

static int shim_input_pre_dispatch(struct AInputQueue *q, void *event)
{
    (void)q;
    (void)event;
    return 0;
}

static void shim_input_finish(struct AInputQueue *q, void *event, int handled)
{
    (void)q;
    (void)event;
    (void)handled;
}

static int shim_input_event_type(const void *event)
{
    (void)event;
    return 0;
}

/* ---- ANativeWindow / ANativeActivity ---- */

static int shim_window_width(struct ANativeWindow *w) { return w->width; }
static int shim_window_height(struct ANativeWindow *w) { return w->height; }
static int shim_window_format(struct ANativeWindow *w) { return w->format; }
static void shim_window_ref(struct ANativeWindow *w) { (void)w; }

static int shim_window_set_buffers_geometry(struct ANativeWindow *w, int width, int height, int format)
{
    if (width > 0 && height > 0) {
        w->width = width;
        w->height = height;
    }
    if (format > 0)
        w->format = format;
    return 0;
}

static void shim_activity_noop(struct hr_native_activity *a, ...)
{
    (void)a; /* ANativeActivity_finish / setWindowFlags / setWindowFormat: nothing to ask the OS for */
}

/* ---- AConfiguration: the values of a typical portrait phone ---- */

struct AConfiguration {
    int mcc, mnc, orientation, touchscreen, density, keyboard, navigation, keys_hidden, nav_hidden, sdk,
        screen_size, screen_long, ui_mode_type, ui_mode_night;
    char language[2], country[2];
};

static struct AConfiguration *shim_config_new(void)
{
    struct AConfiguration *c = calloc(1, sizeof *c);
    if (c)
        *c = (struct AConfiguration){.orientation = 1 /* PORT */, .touchscreen = 3 /* FINGER */, .density = 480,
                                     .keyboard = 1 /* NOKEYS */, .navigation = 1 /* NONAV */,
                                     .keys_hidden = 1 /* NO */, .nav_hidden = 1 /* NO */, .sdk = 35,
                                     .screen_size = 2 /* NORMAL */, .screen_long = 1 /* NO */,
                                     .ui_mode_type = 1 /* NORMAL */, .ui_mode_night = 1 /* NO */,
                                     .language = {'e', 'n'}, .country = {'U', 'S'}};
    return c;
}

static void shim_config_delete(struct AConfiguration *c)
{
    free(c);
}

static void shim_config_from_asset_manager(struct AConfiguration *c, void *mgr)
{
    (void)c;
    (void)mgr; /* no resource tables: keep the defaults */
}

static void shim_config_get_language(struct AConfiguration *c, char *out)
{
    memcpy(out, c->language, 2);
}

static void shim_config_get_country(struct AConfiguration *c, char *out)
{
    memcpy(out, c->country, 2);
}

#define CONFIG_GETTER(fn, field) \
    static int fn(struct AConfiguration *c) { return c->field; }
CONFIG_GETTER(shim_config_mcc, mcc)
CONFIG_GETTER(shim_config_mnc, mnc)
CONFIG_GETTER(shim_config_orientation, orientation)
CONFIG_GETTER(shim_config_touchscreen, touchscreen)
CONFIG_GETTER(shim_config_density, density)
CONFIG_GETTER(shim_config_keyboard, keyboard)
CONFIG_GETTER(shim_config_navigation, navigation)
CONFIG_GETTER(shim_config_keys_hidden, keys_hidden)
CONFIG_GETTER(shim_config_nav_hidden, nav_hidden)
CONFIG_GETTER(shim_config_sdk, sdk)
CONFIG_GETTER(shim_config_screen_size, screen_size)
CONFIG_GETTER(shim_config_screen_long, screen_long)
CONFIG_GETTER(shim_config_ui_mode_type, ui_mode_type)
CONFIG_GETTER(shim_config_ui_mode_night, ui_mode_night)

#define E HR_SHIM
const struct hr_shim_entry hr_shim_android[] = {
    E("__android_log_print", shim_log_print),
    E("__android_log_write", shim_log_write),
    E("ALooper_prepare", shim_looper_prepare),
    E("ALooper_forThread", shim_looper_for_thread),
    E("ALooper_acquire", shim_looper_acquire),
    E("ALooper_release", shim_looper_release),
    E("ALooper_wake", shim_looper_wake),
    E("ALooper_addFd", shim_looper_add_fd),
    E("ALooper_removeFd", shim_looper_remove_fd),
    E("ALooper_pollOnce", shim_looper_poll_once),
    E("ALooper_pollAll", shim_looper_poll_all),
    E("AInputQueue_attachLooper", shim_input_attach),
    E("AInputQueue_detachLooper", shim_input_detach),
    E("AInputQueue_hasEvents", shim_input_has_events),
    E("AInputQueue_getEvent", shim_input_get_event),
    E("AInputQueue_preDispatchEvent", shim_input_pre_dispatch),
    E("AInputQueue_finishEvent", shim_input_finish),
    E("AInputEvent_getType", shim_input_event_type),
    E("ANativeWindow_getWidth", shim_window_width),
    E("ANativeWindow_getHeight", shim_window_height),
    E("ANativeWindow_getFormat", shim_window_format),
    E("ANativeWindow_acquire", shim_window_ref),
    E("ANativeWindow_release", shim_window_ref),
    E("ANativeWindow_setBuffersGeometry", shim_window_set_buffers_geometry),
    E("ANativeActivity_finish", shim_activity_noop),
    E("ANativeActivity_setWindowFlags", shim_activity_noop),
    E("ANativeActivity_setWindowFormat", shim_activity_noop),
    E("AConfiguration_new", shim_config_new),
    E("AConfiguration_delete", shim_config_delete),
    E("AConfiguration_fromAssetManager", shim_config_from_asset_manager),
    E("AConfiguration_getLanguage", shim_config_get_language),
    E("AConfiguration_getCountry", shim_config_get_country),
    E("AConfiguration_getMcc", shim_config_mcc),
    E("AConfiguration_getMnc", shim_config_mnc),
    E("AConfiguration_getOrientation", shim_config_orientation),
    E("AConfiguration_getTouchscreen", shim_config_touchscreen),
    E("AConfiguration_getDensity", shim_config_density),
    E("AConfiguration_getKeyboard", shim_config_keyboard),
    E("AConfiguration_getNavigation", shim_config_navigation),
    E("AConfiguration_getKeysHidden", shim_config_keys_hidden),
    E("AConfiguration_getNavHidden", shim_config_nav_hidden),
    E("AConfiguration_getSdkVersion", shim_config_sdk),
    E("AConfiguration_getScreenSize", shim_config_screen_size),
    E("AConfiguration_getScreenLong", shim_config_screen_long),
    E("AConfiguration_getUiModeType", shim_config_ui_mode_type),
    E("AConfiguration_getUiModeNight", shim_config_ui_mode_night),
};
#undef E
const size_t hr_shim_android_count = sizeof hr_shim_android / sizeof hr_shim_android[0];
