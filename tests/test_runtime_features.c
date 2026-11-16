#define _DARWIN_C_SOURCE
#include "henge/henge.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static atomic_int winners;
static void *claim(void *unused)
{
    (void)unused; char err[512];
    if (!henge_guest_begin(err, sizeof err)) atomic_fetch_add(&winners, 1);
    return NULL;
}
static int child(const char *mode, const char *library, const char *record)
{
    struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
    char err[512];
    assert(!henge_crash_install(record, err, sizeof err));
    henge_image *im = henge_load(library, err, sizeof err);
    if (!im) { fprintf(stderr, "%s\n", err); return 2; }
    int (*unused)(void) = henge_dlsym(im, "unused_missing");
    assert(unused && unused() == 42); /* unused strong import loads, weak remains zero */
    void (*fault)(void) = henge_dlsym(im, !strcmp(mode, "missing") ? "call_missing" : "crash_here");
    assert(fault); fault();
    return 3;
}
int main(int argc, char **argv)
{
    if (argc == 5) return child(argv[1], argv[2], argv[3]);
    assert(argc == 3);
    pthread_t threads[12];
    for (size_t i = 0; i < 12; i++) assert(!pthread_create(&threads[i], NULL, claim, NULL));
    for (size_t i = 0; i < 12; i++) pthread_join(threads[i], NULL);
    assert(atomic_load(&winners) == 1 && henge_guest_started());
    char record[] = "/tmp/henge-crash-XXXXXX";
    int fd = mkstemp(record); assert(fd >= 0); close(fd);
    for (int i = 0; i < 2; i++) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) { execl(argv[0], argv[0], i ? "missing" : "fault", argv[1], record, "child", NULL); _exit(127); }
        int status; assert(waitpid(pid, &status, 0) == pid);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == (i ? SIGABRT : SIGTRAP));
        char small[8], report[16384];
        assert(henge_crash_read(record, small, sizeof small) == -1); /* don't consume on truncation */
        assert(henge_crash_read(record, report, sizeof report) == 1);
        assert(strstr(report, "libmissing.dylib") && strstr(report, "PC 0x") && strstr(report, "x28 0x"));
        if (i) assert(strstr(report, "Unsupported native function: henge_absent_function"));
        assert(henge_crash_read(record, report, sizeof report) == 0);
    }
    char err[512];
    assert(!henge_load(argv[2], err, sizeof err));
    assert(strstr(err, "henge_absent_data"));
    unlink(record);
    puts("PASS: test_runtime_features (one guest, missing-function stubs, data rejection, crash capture/consume)");
    return 0;
}
