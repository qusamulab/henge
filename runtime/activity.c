/* NativeActivity host: runs a library built against <android/native_activity.h> (typically
 * native_app_glue) by playing the part of the Android framework. The window is a placeholder
 * and no input is delivered yet. */
#include "android.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define PLACEHOLDER_WIDTH 390
#define PLACEHOLDER_HEIGHT 844
#define WINDOW_FORMAT_RGBA_8888 1
#define SDK_VERSION 35

struct henge_activity {
    struct hr_native_activity a;
    struct hr_native_activity_callbacks cb;
    struct ANativeWindow window;
    struct AInputQueue *queue;
    char *data_path;
};

typedef void (*create_fn)(struct hr_native_activity *, void *saved_state, size_t saved_size);

henge_activity *henge_activity_start(henge_image *lib, const char *data_path, char *err, size_t errlen)
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
    h->data_path = strdup(data_path ? data_path : "");
    h->queue = hr_input_queue_new();
    if (!h->data_path || !h->queue) {
        hr_fail(err, errlen, "out of memory");
        hr_input_queue_free(h->queue);
        free(h->data_path);
        free(h);
        return NULL;
    }
    h->window = (struct ANativeWindow){PLACEHOLDER_WIDTH, PLACEHOLDER_HEIGHT, WINDOW_FORMAT_RGBA_8888};
    h->a.callbacks = &h->cb;
    h->a.internal_data_path = h->a.external_data_path = h->a.obb_path = h->data_path;
    h->a.sdk_version = SDK_VERSION;

    on_create(&h->a, NULL, 0);
    struct hr_native_activity_callbacks *cb = &h->cb;
    if (cb->onStart) cb->onStart(&h->a);
    if (cb->onResume) cb->onResume(&h->a);
    if (cb->onNativeWindowCreated) cb->onNativeWindowCreated(&h->a, &h->window);
    if (cb->onInputQueueCreated) cb->onInputQueueCreated(&h->a, h->queue);
    if (cb->onWindowFocusChanged) cb->onWindowFocusChanged(&h->a, 1);
    return h;
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
    hr_input_queue_free(h->queue);
    free(h->data_path);
    free(h);
}
