/* NativeActivity host: runs a library built against <android/native_activity.h> (typically
 * native_app_glue) by playing the part of the Android framework: the activity object (with a stub
 * JNI and the APK's assets), the window, the input queue and the lifecycle callbacks. */
#include "android.h"
#include "internal.h"
#include "henge/henge.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PLACEHOLDER_WIDTH 390
#define PLACEHOLDER_HEIGHT 844
#define WINDOW_FORMAT_RGBA_8888 1
#define SDK_VERSION 35

#define MOTION_DOWN 0
#define MOTION_UP 1
#define MOTION_MOVE 2
#define MOTION_CANCEL 3
#define MOTION_POINTER_DOWN 5
#define MOTION_POINTER_UP 6

struct henge_activity {
    struct hr_native_activity a;
    struct hr_native_activity_callbacks cb;
    struct ANativeWindow window;
    struct AInputQueue *queue;
    struct AAssetManager *assets;
    char *data_path;

    pthread_mutex_t touch_lock;
    int nfingers;
    struct {
        int id;
        float x, y;
    } fingers[HR_MAX_POINTERS];
    int64_t down_time_ns;
};

typedef void (*create_fn)(struct hr_native_activity *, void *saved_state, size_t saved_size);

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

henge_activity *henge_activity_start(henge_image *lib, const struct henge_activity_opts *opts, char *err,
                                     size_t errlen)
{
    create_fn on_create = (create_fn)henge_dlsym(lib, "ANativeActivity_onCreate");
    if (!on_create) {
        hr_fail(err, errlen, "ANativeActivity_onCreate is not exported (not a NativeActivity library)");
        return NULL;
    }
    henge_activity *h = calloc(1, sizeof *h);
    if (!h) {
        hr_fail(err, errlen, "out of memory");
        return NULL;
    }
    h->data_path = strdup(opts->data_path ? opts->data_path : "");
    h->queue = hr_input_queue_new();
    if (!h->data_path || !h->queue) {
        hr_fail(err, errlen, "out of memory");
        hr_input_queue_free(h->queue);
        free(h->data_path);
        free(h);
        return NULL;
    }
    if (opts->apk_path) {
        h->assets = hr_asset_manager_new(opts->apk_path);
        hr_asset_manager_set_default(h->assets);
    }
    pthread_mutex_init(&h->touch_lock, NULL);
    h->window.width = opts->width > 0 ? opts->width : PLACEHOLDER_WIDTH;
    h->window.height = opts->height > 0 ? opts->height : PLACEHOLDER_HEIGHT;
    h->window.format = WINDOW_FORMAT_RGBA_8888;
    h->window.layer = opts->layer;
    h->a.callbacks = &h->cb;
    h->a.vm = hr_jni_vm;
    h->a.env = hr_jni_env;
    h->a.clazz = &h->a; /* an opaque jobject: nothing can dereference it through the stub JNI */
    h->a.internal_data_path = h->a.external_data_path = h->a.obb_path = h->data_path;
    h->a.sdk_version = SDK_VERSION;
    h->a.asset_manager = h->assets;

    on_create(&h->a, NULL, 0);
    struct hr_native_activity_callbacks *cb = &h->cb;
    if (cb->onStart) cb->onStart(&h->a);
    if (cb->onResume) cb->onResume(&h->a);
    if (cb->onNativeWindowCreated) cb->onNativeWindowCreated(&h->a, &h->window);
    if (cb->onInputQueueCreated) cb->onInputQueueCreated(&h->a, h->queue);
    if (cb->onWindowFocusChanged) cb->onWindowFocusChanged(&h->a, 1);
    return h;
}

void henge_activity_resize(henge_activity *h, int width, int height)
{
    if (!h || (h->window.width == width && h->window.height == height))
        return;
    h->window.width = width;
    h->window.height = height;
    if (h->cb.onNativeWindowResized) h->cb.onNativeWindowResized(&h->a, &h->window);
    if (h->cb.onNativeWindowRedrawNeeded) h->cb.onNativeWindowRedrawNeeded(&h->a, &h->window);
}

void henge_activity_touch(henge_activity *h, enum henge_touch_phase phase, int id, float x, float y)
{
    if (!h)
        return;
    struct AInputEvent ev = {.type = AL_INPUT_TYPE_MOTION, .source = AL_SOURCE_TOUCHSCREEN};
    pthread_mutex_lock(&h->touch_lock);
    int idx = -1;
    for (int i = 0; i < h->nfingers; i++)
        if (h->fingers[i].id == id)
            idx = i;
    int64_t now = now_ns();

    if (phase == HENGE_TOUCH_BEGAN) {
        if (idx >= 0 || h->nfingers == HR_MAX_POINTERS) {
            pthread_mutex_unlock(&h->touch_lock);
            return;
        }
        idx = h->nfingers++;
        h->fingers[idx].id = id;
        if (idx == 0) h->down_time_ns = now;
        ev.action = idx == 0 ? MOTION_DOWN : MOTION_POINTER_DOWN | (idx << 8);
    } else if (idx < 0) {
        pthread_mutex_unlock(&h->touch_lock);
        return;
    } else {
        ev.action = phase == HENGE_TOUCH_MOVED   ? MOTION_MOVE
                    : phase == HENGE_TOUCH_ENDED ? (h->nfingers == 1 ? MOTION_UP : MOTION_POINTER_UP | (idx << 8))
                                                 : MOTION_CANCEL;
    }
    h->fingers[idx].x = x;
    h->fingers[idx].y = y;

    ev.down_time_ns = h->down_time_ns;
    ev.event_time_ns = now;
    ev.npointers = h->nfingers;
    for (int i = 0; i < h->nfingers; i++) {
        ev.pointers[i].id = h->fingers[i].id;
        ev.pointers[i].x = h->fingers[i].x;
        ev.pointers[i].y = h->fingers[i].y;
    }
    /* the finger that went up is still in this event; drop it afterwards */
    if (phase == HENGE_TOUCH_ENDED) {
        memmove(&h->fingers[idx], &h->fingers[idx + 1], (size_t)(h->nfingers - idx - 1) * sizeof h->fingers[0]);
        h->nfingers--;
    } else if (phase == HENGE_TOUCH_CANCELLED) {
        h->nfingers = 0;
    }
    pthread_mutex_unlock(&h->touch_lock);
    hr_input_queue_push(h->queue, &ev);
}

void henge_activity_stop(henge_activity *h)
{
    if (!h)
        return;
    struct hr_native_activity_callbacks *cb = &h->cb;
    if (cb->onWindowFocusChanged) cb->onWindowFocusChanged(&h->a, 0);
    if (cb->onInputQueueDestroyed) cb->onInputQueueDestroyed(&h->a, h->queue);
    if (cb->onNativeWindowDestroyed) cb->onNativeWindowDestroyed(&h->a, &h->window);
    if (cb->onPause) cb->onPause(&h->a);
    if (cb->onStop) cb->onStop(&h->a);
    if (cb->onDestroy) cb->onDestroy(&h->a);
    hr_asset_manager_set_default(NULL);
    hr_asset_manager_free(h->assets);
    hr_input_queue_free(h->queue);
    pthread_mutex_destroy(&h->touch_lock);
    free(h->data_path);
    free(h);
}
