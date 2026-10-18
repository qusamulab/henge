/* Minimal NativeActivity (native_app_glue): records which lifecycle events android_main saw. */
#include <android/log.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <stdlib.h>

enum { STAGE_MAIN = 1, STAGE_WINDOW = 2, STAGE_FOCUS = 4, STAGE_DESTROY = 8 };
static volatile int g_stage;

static volatile int g_touches, g_pointers, g_asset_len = -1;
static volatile float g_last_x, g_last_y;
static volatile int g_last_action;

int native_stage(void) { return g_stage; }
int native_touches(void) { return g_touches; }
int native_pointers(void) { return g_pointers; }
int native_last_action(void) { return g_last_action; }
float native_last_x(void) { return g_last_x; }
float native_last_y(void) { return g_last_y; }
int native_asset_len(void) { return g_asset_len; }

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    g_last_action = AMotionEvent_getAction(ev);
    g_pointers = (int)AMotionEvent_getPointerCount(ev);
    g_last_x = AMotionEvent_getX(ev, 0);
    g_last_y = AMotionEvent_getY(ev, 0);
    g_touches++;
    return 1;
}

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
    app->onInputEvent = on_input;
    AAsset *a = AAssetManager_open(app->activity->assetManager, "hello.txt", AASSET_MODE_BUFFER);
    if (a) {
        g_asset_len = (int)AAsset_getLength(a);
        AAsset_close(a);
    }
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
