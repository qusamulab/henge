/* Platform-facing Dalvik Activity host API. */
#ifndef HENGE_VM_HOST_H
#define HENGE_VM_HOST_H
#include <stddef.h>
typedef struct vm_host vm_host;

struct vm_host_opts {
    const char *apk_path;
    void *window;        /* struct ANativeWindow* (runtime/android.h) backing the GLSurfaceView */
    int width, height;   /* window size in pixels */
    void *layer;          /* CAEAGLLayer if window == NULL; owned stand-in ANativeWindow */
    void (*log)(int prio, const char *tag, const char *msg, void *user); /* android.util.Log and uncaught exceptions */
    void *user;
};

/* Starts the activity on its own thread and returns once onCreate and the first surface callbacks have run
 * (NULL with a message in err on failure). */
vm_host *vm_host_start(const struct vm_host_opts *opts, char *err, size_t errlen);
void vm_host_touch(vm_host *h, int phase, int id, float x, float y); /* phase: 0 down 1 move 2 up 3 cancel */
void vm_host_stop(vm_host *h); /* onPause / onStop / onDestroy, joins the thread */
int vm_host_running(vm_host *h);

void vm_host_resize(vm_host *host, int width, int height);

#endif
