#define _DARWIN_C_SOURCE
#include "vm/vm.h"
#include "henge/henge.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static atomic_int passed;
static void log_line(int priority, const char *tag, const char *message, void *user)
{
    (void)priority; (void)tag; (void)user;
    puts(message);
    if (!strcmp(message, "PASS: Java Activity through JNI")) passed = 1;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    henge_set_library_path(argv[2]);
    char err[512];
    struct vm_host_opts opts = {.apk_path = argv[1], .width = 640, .height = 480, .log = log_line};
    vm_host *host = vm_host_start(&opts, err, sizeof err);
    if (!host) fprintf(stderr, "%s\n", err);
    assert(host);
    for (unsigned i = 0; i < 1000 && vm_host_running(host); i++) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(!vm_host_running(host) && atomic_load(&passed));
    vm_host_stop(host);
    assert(!vm_create()); /* JNI may retain JavaVM even after guest Stop. */
    puts("PASS: test_vm_jni_host (APK launcher -> Activity -> System.loadLibrary -> JNI -> Java callback -> lifecycle Stop)");
    return 0;
}
