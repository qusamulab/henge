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

/* An input event (AInputEvent*): a motion event with up to HR_MAX_POINTERS touches, or a key event. */
#define HR_MAX_POINTERS 10
#define AL_INPUT_TYPE_KEY 1
#define AL_INPUT_TYPE_MOTION 2
#define AL_SOURCE_TOUCHSCREEN 0x1002
#define AL_SOURCE_KEYBOARD 0x101

struct AInputEvent {
    int type;
    int source;
    int action; /* AMOTION_EVENT_ACTION_* | (pointer index << 8), or AKEY_EVENT_ACTION_* */
    int key_code;
    int64_t down_time_ns, event_time_ns;
    int npointers;
    struct {
        int id;
        float x, y;
    } pointers[HR_MAX_POINTERS];
};

/* The object behind AInputQueue*: a FIFO of events plus a pipe whose read end is registered with the
 * looper, one byte per queued event. */
struct AInputQueue {
    int pipe[2];
    struct ALooper *looper;
    pthread_mutex_t lock;
    struct AInputEvent **events;
    size_t nevents, cap;
};

/* Thread-safe; copies the event. */
void hr_input_queue_push(struct AInputQueue *q, const struct AInputEvent *ev);

/* The object behind ANativeWindow*: a placeholder surface of a given size. */
struct ANativeWindow {
    int width, height, format;
    void *layer; /* the platform surface (a CAEAGLLayer on iOS), or NULL for a placeholder */
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
struct AAssetManager;
struct AAssetManager *hr_asset_manager_new(const char *apk_path);
void hr_asset_manager_free(struct AAssetManager *mgr);
void hr_asset_manager_set_default(struct AAssetManager *mgr); /* what AAssetManager_fromJava returns */
extern void *hr_jni_vm, *hr_jni_env; /* stub JavaVM* / JNIEnv* for activity->vm / env */
void hr_input_queue_free(struct AInputQueue *q);

#endif
