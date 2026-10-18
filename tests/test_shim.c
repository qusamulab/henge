/* Shim test: runs libshim.so (pthread objects, errno, fd I/O, ALooper, AConfiguration, liblog)
 * converted with henge-convert, on macOS. */
#include <dlfcn.h>
#include <stdio.h>

#include "henge/henge.h"

static int failures;

static void run(henge_image *img, const char *name)
{
    int (*fn)(void) = (int (*)(void))henge_dlsym(img, name);
    if (!fn) {
        printf("FAIL  henge_dlsym(\"%s\") returned NULL\n", name);
        failures++;
        return;
    }
    int step = fn();
    if (step == 0) {
        printf("PASS  %s()\n", name);
    } else {
        printf("FAIL  %s() failed at step %d\n", name, step);
        failures++;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s path/to/libshim.dylib\n", argv[0]);
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

    run(img, "t_mutex");
    run(img, "t_thread_cond");
    run(img, "t_fd_errno");
    run(img, "t_looper");
    run(img, "t_config");
    run(img, "t_log");
    run(img, "t_format");

    printf("%s: test_shim (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
