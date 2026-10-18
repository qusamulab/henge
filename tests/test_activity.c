/* NativeActivity host test: runs libnative.so (native_app_glue) through henge_activity_start and
 * checks that android_main, the window and focus events arrive, then tears it down. */
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#include "henge/henge.h"

static int failures;

#define CHECK(desc, cond)                                      \
    do {                                                       \
        int ok_ = (cond);                                      \
        printf("%s  %s\n", ok_ ? "PASS" : "FAIL", desc);       \
        if (!ok_) failures++;                                  \
    } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s path/to/libnative.dylib [apk-with-assets]\n", argv[0]);
        return 2;
    }
    if (!dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) {
        printf("FAIL  dlopen: %s\n", dlerror());
        return 1;
    }
    char err[512] = {0};
    henge_image *img = henge_link(argv[1], err, sizeof err);
    if (!img) {
        printf("FAIL  henge_link: %s\n", err);
        return 1;
    }
    printf("PASS  dlopen + henge_link\n");
    int (*stage)(void) = (int (*)(void))henge_dlsym(img, "native_stage");
    if (!stage) {
        printf("FAIL  henge_dlsym(\"native_stage\") returned NULL\n");
        return 1;
    }

    struct henge_activity_opts opts = {.data_path = "/tmp", .apk_path = argc > 2 ? argv[2] : NULL};
    henge_activity *act = henge_activity_start(img, &opts, err, sizeof err);
    CHECK("henge_activity_start", act != NULL);
    if (!act) {
        printf("info  %s\n", err);
        return 1;
    }
    for (int i = 0; i < 200 && stage() != 7; i++) usleep(10000);
    CHECK("android_main ran (stage 1)", stage() & 1);
    CHECK("window delivered (stage 2)", stage() & 2);
    CHECK("focus delivered (stage 4)", stage() & 4);

    /* assets/hello.txt from the APK, read by android_main through AAssetManager */
    if (argc > 2) {
        int (*asset_len)(void) = (int (*)(void))henge_dlsym(img, "native_asset_len");
        CHECK("AAssetManager reads assets/hello.txt", asset_len && asset_len() > 0);
    }

    /* touches: one finger down/move/up, then two fingers */
    int (*touches)(void) = (int (*)(void))henge_dlsym(img, "native_touches");
    int (*pointers)(void) = (int (*)(void))henge_dlsym(img, "native_pointers");
    int (*last_action)(void) = (int (*)(void))henge_dlsym(img, "native_last_action");
    float (*last_x)(void) = (float (*)(void))henge_dlsym(img, "native_last_x");
    float (*last_y)(void) = (float (*)(void))henge_dlsym(img, "native_last_y");
    henge_activity_touch(act, HENGE_TOUCH_BEGAN, 0, 120.5f, 340.25f);
    for (int i = 0; i < 200 && touches() < 1; i++) usleep(10000);
    CHECK("touch down arrives (ACTION_DOWN, 1 pointer)", touches() == 1 && last_action() == 0 && pointers() == 1);
    CHECK("touch position", last_x() == 120.5f && last_y() == 340.25f);
    henge_activity_touch(act, HENGE_TOUCH_MOVED, 0, 130.0f, 350.0f);
    for (int i = 0; i < 200 && touches() < 2; i++) usleep(10000);
    CHECK("touch move arrives (ACTION_MOVE)", touches() == 2 && last_action() == 2 && last_x() == 130.0f);
    henge_activity_touch(act, HENGE_TOUCH_BEGAN, 1, 50.0f, 60.0f);
    for (int i = 0; i < 200 && touches() < 3; i++) usleep(10000);
    CHECK("second finger (ACTION_POINTER_DOWN, 2 pointers)", touches() == 3 && last_action() == (5 | (1 << 8)) && pointers() == 2);
    henge_activity_touch(act, HENGE_TOUCH_ENDED, 1, 50.0f, 60.0f);
    henge_activity_touch(act, HENGE_TOUCH_ENDED, 0, 130.0f, 350.0f);
    for (int i = 0; i < 200 && touches() < 5; i++) usleep(10000);
    CHECK("fingers up (ACTION_UP)", touches() == 5 && last_action() == 1);

    henge_activity_stop(act);
    CHECK("destroy delivered (stage 8)", stage() & 8);

    printf("%s: test_activity (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
