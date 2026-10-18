/* Android NDK objects implemented by the shim (shim_android.c). */
#ifndef HENGE_ANDROID_H
#define HENGE_ANDROID_H

#include <pthread.h>
#include <stddef.h>

/* ALOOPER_* values from <android/looper.h>. */
#define AL_POLL_WAKE (-1)
#define AL_POLL_CALLBACK (-2)
#define AL_POLL_TIMEOUT (-3)
#define AL_POLL_ERROR (-4)
#define AL_EVENT_INPUT 1
#define AL_EVENT_OUTPUT 2
#define AL_EVENT_ERROR 4
#define AL_EVENT_HANGUP 8

typedef int (*hr_looper_cb)(int fd, int events, void *data);

struct hr_looper_fd {
    int fd, ident, events;
    hr_looper_cb cb;
    void *data;
};

struct hr_looper_ready {
    int fd, ident, events;
    void *data;
};

/* One per thread that called ALooper_prepare (poll(2) over the registered fds + a wake pipe). */
struct ALooper {
    int refs;
    int wake[2];
    pthread_mutex_t lock;
    struct hr_looper_fd *fds;
    size_t nfds;
    struct hr_looper_ready *pending; /* ready callback-less fds, handed out one per poll */
    size_t npending, pending_pos;
};

/* The object behind AInputQueue*: a pipe whose read end is registered with the looper.
 * Input events are not delivered yet. */
struct AInputQueue {
    int pipe[2];
    struct ALooper *looper;
};

/* The object behind ANativeWindow*: a placeholder surface of a given size. */
struct ANativeWindow {
    int width, height, format;
};

/* Layout of <android/native_activity.h> on LP64. */
struct hr_native_activity;
struct hr_native_activity_callbacks {
    void (*onStart)(struct hr_native_activity *);
    void (*onResume)(struct hr_native_activity *);
    void *(*onSaveInstanceState)(struct hr_native_activity *, size_t *out_size);
    void (*onPause)(struct hr_native_activity *);
    void (*onStop)(struct hr_native_activity *);
    void (*onDestroy)(struct hr_native_activity *);
    void (*onWindowFocusChanged)(struct hr_native_activity *, int has_focus);
    void (*onNativeWindowCreated)(struct hr_native_activity *, struct ANativeWindow *);
    void (*onNativeWindowResized)(struct hr_native_activity *, struct ANativeWindow *);
    void (*onNativeWindowRedrawNeeded)(struct hr_native_activity *, struct ANativeWindow *);
    void (*onNativeWindowDestroyed)(struct hr_native_activity *, struct ANativeWindow *);
    void (*onInputQueueCreated)(struct hr_native_activity *, struct AInputQueue *);
    void (*onInputQueueDestroyed)(struct hr_native_activity *, struct AInputQueue *);
    void (*onContentRectChanged)(struct hr_native_activity *, const void *rect);
    void (*onConfigurationChanged)(struct hr_native_activity *);
    void (*onLowMemory)(struct hr_native_activity *);
};

struct hr_native_activity {
    struct hr_native_activity_callbacks *callbacks;
    void *vm;    /* JavaVM*: none yet */
    void *env;   /* JNIEnv*: none yet */
    void *clazz; /* jobject */
    const char *internal_data_path;
    const char *external_data_path;
    int32_t sdk_version;
    void *instance;
    void *asset_manager;
    const char *obb_path;
};

struct AInputQueue *hr_input_queue_new(void);
void hr_input_queue_free(struct AInputQueue *q);

#endif
