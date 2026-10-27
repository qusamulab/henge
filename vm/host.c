/* The host: runs an APK's launcher Activity on henge-vm. One thread does everything an Android app
 * has several threads for (UI, GL, handlers): it creates the activity, owns the EGL surface and loops
 * { touches, Handler callbacks, onDrawFrame, swap }. */
#include "vm.h"
#include "henge/apk.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *henge_shim_lookup(const char *name);

#define TOUCH_QUEUE 256

struct touch { int phase, id; float x, y; };

struct vm_host {
    struct vm_host_opts opts;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t started;
    int start_done, start_ok;
    char start_err[256];
    atomic_int stop;
    atomic_int running;
    struct touch queue[TOUCH_QUEUE];
    int qhead, qtail;
};

static vm_host *g_host; /* the log / asset callbacks have no user pointer */

static void log_line(vm_host *h, int prio, const char *tag, const char *msg)
{
    if (h->opts.log) h->opts.log(prio, tag, msg, h->opts.user);
    else fprintf(stderr, "[%s] %s\n", tag, msg);
}

static void android_log(int prio, const char *tag, const char *msg)
{
    if (g_host) log_line(g_host, prio, tag, msg);
}

/* ---- assets from the APK ---- */

static henge_apk *g_apk;

static void asset_reader(const char *name, uint8_t **data, size_t *size)
{
    char path[512];
    char err[128];
    snprintf(path, sizeof path, "assets/%s", name);
    long i = g_apk ? henge_apk_find(g_apk, path) : -1;
    if (i >= 0) henge_apk_extract(g_apk, (size_t)i, data, size, err, sizeof err);
}

/* ---- calling into the VM ---- */

static void report_exception(vm_host *h, Thread *t, const char *what)
{
    Object *e = vm_obj(t->vm, t->exception);
    char msg[600] = "";
    if (e) {
        Method *ts = vm_lookup_virtual(e->cls, t->vm->sig_to_string);
        Ref saved = t->exception;
        t->exception = 0;
        if (ts) {
            Ref s = (Ref)vm_invoke(t, ts, (uint32_t[]){saved});
            if (!t->exception && s) {
                char *u = vm_string_utf8(t->vm, s);
                snprintf(msg, sizeof msg, "%s", u);
                free(u);
            }
        }
        if (!msg[0]) snprintf(msg, sizeof msg, "%s", e->cls->name);
    }
    t->exception = 0;
    char line[800];
    snprintf(line, sizeof line, "uncaught exception in %s: %s", what, msg);
    log_line(h, 6, "henge-vm", line);
}

/* The implementation of name+proto that a virtual call on an instance of cls reaches, or NULL. */
static Method *find_virtual(Class *cls, const char *name, const char *proto)
{
    Method *m = vm_find_method(cls, name, proto);
    Method *v = m ? vm_lookup_virtual(cls, m->sig) : NULL;
    return v ? v : m;
}

/* Calls an instance method by name; returns 0 and reports on failure. */
static int call(vm_host *h, Thread *t, Ref self, const char *name, const char *proto, uint32_t *extra, int nextra)
{
    Object *o = vm_obj(t->vm, self);
    char sig[300];
    snprintf(sig, sizeof sig, "%s%s", name, proto);
    Method *m = find_virtual(o->cls, name, proto);
    if (!m) {
        char line[400];
        snprintf(line, sizeof line, "%s has no method %s", o->cls->name, sig);
        log_line(h, 6, "henge-vm", line);
        return 0;
    }
    uint32_t args[8] = {self};
    for (int i = 0; i < nextra && i < 7; i++) args[1 + i] = extra[i];
    vm_invoke(t, m, args);
    if (t->exception) {
        report_exception(h, t, name);
        return 0;
    }
    return 1;
}

static void *host_main(void *arg);

struct egl {
    void *dpy, *cfg, *ctx, *surf;
    void *(*get_display)(void *);
    int (*initialize)(void *, int *, int *);
    int (*choose_config)(void *, const int *, void **, int, int *);
    void *(*create_surface)(void *, void *, void *, const int *);
    void *(*create_context)(void *, void *, void *, const int *);
    int (*make_current)(void *, void *, void *, void *);
    int (*swap)(void *, void *);
    int (*query_surface)(void *, void *, int, int *);
    int (*destroy_surface)(void *, void *);
    int (*destroy_context)(void *, void *);
};

static int egl_setup(struct egl *e, void *window, int gl_version, int *w, int *h)
{
    e->get_display = henge_shim_lookup("eglGetDisplay");
    e->initialize = henge_shim_lookup("eglInitialize");
    e->choose_config = henge_shim_lookup("eglChooseConfig");
    e->create_surface = henge_shim_lookup("eglCreateWindowSurface");
    e->create_context = henge_shim_lookup("eglCreateContext");
    e->make_current = henge_shim_lookup("eglMakeCurrent");
    e->swap = henge_shim_lookup("eglSwapBuffers");
    e->query_surface = henge_shim_lookup("eglQuerySurface");
    e->destroy_surface = henge_shim_lookup("eglDestroySurface");
    e->destroy_context = henge_shim_lookup("eglDestroyContext");
    if (!e->get_display || !e->create_surface || !e->create_context || !e->make_current || !e->swap || !window)
        return -1;
    e->dpy = e->get_display(NULL);
    e->initialize(e->dpy, NULL, NULL);
    const int cfg_attr[] = {0x3040, 4, 0x3033, 4, 0x3038};
    int n;
    e->choose_config(e->dpy, cfg_attr, &e->cfg, 1, &n);
    e->surf = e->create_surface(e->dpy, e->cfg, window, NULL);
    const int ctx_attr[] = {0x3098, gl_version, 0x3038};
    e->ctx = e->create_context(e->dpy, e->cfg, NULL, ctx_attr);
    if (!e->surf || !e->ctx || !e->make_current(e->dpy, e->surf, e->surf, e->ctx))
        return -1;
    e->query_surface(e->dpy, e->surf, 0x3057, w);
    e->query_surface(e->dpy, e->surf, 0x3056, h);
    return 0;
}

/* ---- the thread ---- */

static void start_result(vm_host *h, int ok, const char *err)
{
    pthread_mutex_lock(&h->lock);
    h->start_ok = ok;
    if (err) snprintf(h->start_err, sizeof h->start_err, "%s", err);
    h->start_done = 1;
    pthread_cond_broadcast(&h->started);
    pthread_mutex_unlock(&h->lock);
}

static uint8_t *read_apk_entry(henge_apk *apk, const char *name, size_t *size)
{
    long i = henge_apk_find(apk, name);
    if (i < 0) return NULL;
    uint8_t *data;
    char err[128];
    return henge_apk_extract(apk, (size_t)i, &data, size, err, sizeof err) == 0 ? data : NULL;
}

static void *host_main(void *arg)
{
    vm_host *h = arg;
    char err[512];
    VM *vm = NULL;
    uint8_t *dex_data[31] = {0};
    size_t ndex = 0;
    struct egl egl = {0};
    g_host = h;
    vm_android_log = android_log;
    vm_asset_reader = asset_reader;

    g_apk = henge_apk_open(h->opts.apk_path, err, sizeof err);
    if (!g_apk) { start_result(h, 0, err); goto cleanup; }
    struct henge_apk_manifest man;
    if (henge_apk_read_manifest(g_apk, &man, err, sizeof err) != 0) { start_result(h, 0, err); goto cleanup; }
    if (!man.launcher_activity[0]) { start_result(h, 0, "the manifest has no MAIN / LAUNCHER activity"); goto cleanup; }

    vm = vm_create();
    if (!vm) { start_result(h, 0, "could not create the VM"); goto cleanup; }
    Thread *t = vm_main_thread(vm);

    for (int i = 1; i < 32; i++) {
        char name[32];
        if (i == 1) snprintf(name, sizeof name, "classes.dex");
        else snprintf(name, sizeof name, "classes%d.dex", i);
        size_t size;
        uint8_t *data = read_apk_entry(g_apk, name, &size);
        if (!data) break;
        dex_data[ndex++] = data;
        if (vm_add_dex(vm, data, size, err, sizeof err) != 0) { start_result(h, 0, err); goto cleanup; }
    }
    if (!ndex) { start_result(h, 0, "the APK has no classes.dex"); goto cleanup; }

    char desc[300];
    snprintf(desc, sizeof desc, "L%s;", man.launcher_activity);
    for (char *p = desc; *p; p++) if (*p == '.') *p = '/';
    Class *ac = vm_find_class(vm, desc);
    Class *activity_base = vm_find_class(vm, "Landroid/app/Activity;");
    if (!ac || !vm_instance_of(ac, activity_base)) {
        snprintf(err, sizeof err, "%s is not an android.app.Activity", man.launcher_activity);
        start_result(h, 0, err);
        goto cleanup;
    }
    if (vm_init_class(t, ac) != 0) { report_exception(h, t, "class init"); start_result(h, 0, "activity class initialization failed"); goto cleanup; }
    Ref act = vm_new_object(t, ac);
    vm_pin(vm, act);
    Method *ctor = vm_find_method(ac, "<init>", "()V");
    if (!ctor) { start_result(h, 0, "the activity has no no-argument constructor"); goto cleanup; }
    vm_invoke(t, ctor, (uint32_t[]){act});
    if (t->exception) { report_exception(h, t, "constructor"); start_result(h, 0, "activity constructor threw"); goto cleanup; }

    Ref bundle = vm_new_object(t, vm_find_class(vm, "Landroid/os/Bundle;"));
    vm_pin(vm, bundle);
    {
        Method *bc = vm_find_method(vm_find_class(vm, "Landroid/os/Bundle;"), "<init>", "()V");
        vm_invoke(t, bc, (uint32_t[]){bundle});
    }
    if (!call(h, t, act, "henge_create", "(Landroid/os/Bundle;)V", (uint32_t[]){bundle}, 1)) { start_result(h, 0, "onCreate threw"); goto cleanup; }
    call(h, t, act, "henge_start", "()V", NULL, 0);
    call(h, t, act, "henge_resume", "()V", NULL, 0);

    /* the content view and, for a GLSurfaceView, the EGL surface */
    Object *ao = vm_obj(vm, act);
    Ref view = ao->u.words[vm_field_offset(ac, "henge_contentView")];
    int have_gl = 0, w = h->opts.width, hh = h->opts.height;
    Class *glview_cls = vm_find_class(vm, "Landroid/opengl/GLSurfaceView;");
    if (view && vm_instance_of(vm_obj(vm, view)->cls, glview_cls)) {
        vm_pin(vm, view);
        int ver = (int)vm_obj(vm, view)->u.words[vm_field_offset(glview_cls, "henge_glVersion")];
        if (egl_setup(&egl, h->opts.window, ver, &w, &hh) != 0) {
            start_result(h, 0, "could not create the EGL surface");
            goto cleanup;
        }
        have_gl = 1;
        Object *vo = vm_obj(vm, view);
        vo->u.words[vm_field_offset(glview_cls, "henge_width")] = (uint32_t)w;
        vo->u.words[vm_field_offset(glview_cls, "henge_height")] = (uint32_t)hh;
        call(h, t, view, "henge_surfaceCreated", "()V", NULL, 0);
        call(h, t, view, "henge_surfaceChanged", "(II)V", (uint32_t[]){(uint32_t)w, (uint32_t)hh}, 2);
    } else if (view) {
        vm_pin(vm, view);
        log_line(h, 5, "henge-vm", "the content view is not a GLSurfaceView: nothing to draw");
    }
    call(h, t, act, "henge_focus", "(Z)V", (uint32_t[]){1}, 1);

    /* MotionEvent reused for every touch */
    Class *mec = vm_find_class(vm, "Landroid/view/MotionEvent;");
    Ref me = vm_new_object(t, mec);
    vm_pin(vm, me);
    {
        Method *mc = vm_find_method(mec, "<init>", "()V");
        vm_invoke(t, mc, (uint32_t[]){me});
    }
    struct { int id; float x, y; } fingers[10];
    int nfingers = 0;

    /* Resolved once: the loop below runs every frame (~250/s) and on every touch. */
    uint32_t off_finished = vm_field_offset(ac, "henge_finished");
    uint32_t off_action = vm_field_offset(mec, "action"), off_count = vm_field_offset(mec, "count");
    uint32_t off_ids = vm_field_offset(mec, "ids"), off_xs = vm_field_offset(mec, "xs"), off_ys = vm_field_offset(mec, "ys");
    Class *handler_cls = vm_find_class(vm, "Landroid/os/Handler;");
    Method *run_pending = handler_cls ? vm_find_method(handler_cls, "runPending", "()V") : NULL;
    Method *draw_frame = have_gl ? find_virtual(vm_obj(vm, view)->cls, "henge_drawFrame", "()Z") : NULL;
    Method *dispatch_touch = view ? find_virtual(vm_obj(vm, view)->cls, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z") : NULL;
    if (off_finished == UINT32_MAX || off_action == UINT32_MAX || off_count == UINT32_MAX || off_ids == UINT32_MAX ||
        off_xs == UINT32_MAX || off_ys == UINT32_MAX || !run_pending || (have_gl && !draw_frame) || (view && !dispatch_touch)) {
        start_result(h, 0, "henge's android classes lack a member the host needs");
        goto cleanup;
    }
    if (vm_init_class(t, handler_cls) != 0) { report_exception(h, t, "Handler"); start_result(h, 0, "Handler.<clinit> threw"); goto cleanup; }

    h->running = 1;
    start_result(h, 1, NULL);

    while (!h->stop) {
        /* touches */
        for (;;) {
            struct touch tc;
            pthread_mutex_lock(&h->lock);
            int have = h->qhead != h->qtail;
            if (have) { tc = h->queue[h->qtail]; h->qtail = (h->qtail + 1) % TOUCH_QUEUE; }
            pthread_mutex_unlock(&h->lock);
            if (!have) break;
            int idx = -1;
            for (int i = 0; i < nfingers; i++) if (fingers[i].id == tc.id) idx = i;
            int action;
            if (tc.phase == 0) {
                if (idx >= 0 || nfingers == 10) continue;
                idx = nfingers++;
                fingers[idx].id = tc.id;
                action = idx == 0 ? 0 : (5 | (idx << 8));
            } else if (idx < 0) {
                continue;
            } else {
                action = tc.phase == 1 ? 2 : tc.phase == 2 ? (nfingers == 1 ? 1 : (6 | (idx << 8))) : 3;
            }
            fingers[idx].x = tc.x;
            fingers[idx].y = tc.y;
            Object *e = vm_obj(vm, me);
            e->u.words[off_action] = (uint32_t)action;
            e->u.words[off_count] = (uint32_t)nfingers;
            Object *ids = vm_obj(vm, e->u.words[off_ids]);
            Object *xs = vm_obj(vm, e->u.words[off_xs]);
            Object *ys = vm_obj(vm, e->u.words[off_ys]);
            for (int i = 0; i < nfingers; i++) {
                ((int32_t *)ids->u.bytes)[i] = fingers[i].id;
                ((float *)xs->u.bytes)[i] = fingers[i].x;
                ((float *)ys->u.bytes)[i] = fingers[i].y;
            }
            if (view) {
                vm_invoke(t, dispatch_touch, (uint32_t[]){view, me});
                if (t->exception) report_exception(h, t, "dispatchTouchEvent");
            }
            if (tc.phase == 2) {
                memmove(&fingers[idx], &fingers[idx + 1], (size_t)(nfingers - idx - 1) * sizeof fingers[0]);
                nfingers--;
            } else if (tc.phase == 3) {
                nfingers = 0;
            }
        }

        vm_invoke(t, run_pending, NULL);
        if (t->exception) { report_exception(h, t, "Handler"); break; }

        if (have_gl) {
            Ref drawn = (Ref)vm_invoke(t, draw_frame, (uint32_t[]){view});
            if (t->exception) { report_exception(h, t, "onDrawFrame"); break; }
            if (drawn & 0xff) egl.swap(egl.dpy, egl.surf);
        }
        if (ao = vm_obj(vm, act), ao->u.words[off_finished] & 0xff) break;
        usleep(4000);
    }

    call(h, t, act, "henge_focus", "(Z)V", (uint32_t[]){0}, 1);
    call(h, t, act, "henge_pause", "()V", NULL, 0);
    call(h, t, act, "henge_stop", "()V", NULL, 0);
    call(h, t, act, "henge_destroy", "()V", NULL, 0);
cleanup:
    if (egl.dpy) {
        if (egl.make_current) egl.make_current(egl.dpy, NULL, NULL, NULL);
        if (egl.surf && egl.destroy_surface) egl.destroy_surface(egl.dpy, egl.surf);
        if (egl.ctx && egl.destroy_context) egl.destroy_context(egl.dpy, egl.ctx);
    }
    h->running = 0;
    vm_android_log = NULL;
    vm_asset_reader = NULL;
    henge_apk_close(g_apk);
    g_apk = NULL;
    vm_destroy(vm);
    for (size_t i = 0; i < ndex; i++)
        free(dex_data[i]);
    g_host = NULL;
    return NULL;
}

vm_host *vm_host_start(const struct vm_host_opts *opts, char *err, size_t errlen)
{
    vm_host *h = calloc(1, sizeof *h);
    if (!h) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    h->opts = *opts;
    atomic_init(&h->stop, 0);
    atomic_init(&h->running, 0);
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->started, NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    int rc = pthread_create(&h->thread, &attr, host_main, h);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        snprintf(err, errlen, "could not start the VM thread");
        pthread_mutex_destroy(&h->lock);
        pthread_cond_destroy(&h->started);
        free(h);
        return NULL;
    }
    pthread_mutex_lock(&h->lock);
    while (!h->start_done) pthread_cond_wait(&h->started, &h->lock);
    pthread_mutex_unlock(&h->lock);
    if (!h->start_ok) {
        snprintf(err, errlen, "%s", h->start_err);
        pthread_join(h->thread, NULL);
        pthread_mutex_destroy(&h->lock);
        pthread_cond_destroy(&h->started);
        free(h);
        return NULL;
    }
    return h;
}

void vm_host_touch(vm_host *h, int phase, int id, float x, float y)
{
    pthread_mutex_lock(&h->lock);
    int next = (h->qhead + 1) % TOUCH_QUEUE;
    if (next != h->qtail) {
        h->queue[h->qhead] = (struct touch){phase, id, x, y};
        h->qhead = next;
    }
    pthread_mutex_unlock(&h->lock);
}

int vm_host_running(vm_host *h) { return h->running; }

void vm_host_stop(vm_host *h)
{
    h->stop = 1;
    pthread_join(h->thread, NULL);
    pthread_mutex_destroy(&h->lock);
    pthread_cond_destroy(&h->started);
    free(h);
}
