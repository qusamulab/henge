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
    if (argc != 2) {
        fprintf(stderr, "usage: %s path/to/libnative.dylib\n", argv[0]);
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

    henge_activity *act = henge_activity_start(img, "/tmp", err, sizeof err);
    CHECK("henge_activity_start", act != NULL);
    if (!act) {
        printf("info  %s\n", err);
        return 1;
    }
    for (int i = 0; i < 200 && stage() != 7; i++) usleep(10000);
    CHECK("android_main ran (stage 1)", stage() & 1);
    CHECK("window delivered (stage 2)", stage() & 2);
    CHECK("focus delivered (stage 4)", stage() & 4);

    henge_activity_stop(act);
    CHECK("destroy delivered (stage 8)", stage() & 8);

    printf("%s: test_activity (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
