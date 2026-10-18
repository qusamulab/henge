/* Minimal NativeActivity (native_app_glue): records which lifecycle events android_main saw. */
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

enum { STAGE_MAIN = 1, STAGE_WINDOW = 2, STAGE_FOCUS = 4, STAGE_DESTROY = 8 };
static volatile int g_stage;

int native_stage(void) { return g_stage; }

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        __android_log_print(ANDROID_LOG_INFO, "henge", "window %dx%d", ANativeWindow_getWidth(app->window),
                            ANativeWindow_getHeight(app->window));
        g_stage |= STAGE_WINDOW;
        break;
    case APP_CMD_GAINED_FOCUS: g_stage |= STAGE_FOCUS; break;
    case APP_CMD_DESTROY: g_stage |= STAGE_DESTROY; break;
    }
}

void android_main(struct android_app *app) {
    app->onAppCmd = on_cmd;
    __android_log_print(ANDROID_LOG_INFO, "henge", "android_main");
    g_stage |= STAGE_MAIN;
    for (;;) {
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(-1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) return;
        }
    }
}
