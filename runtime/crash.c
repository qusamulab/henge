/* Signal-side work is limited to lock-free snapshot reads and write/kill/sigaction. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

#define CRASH_MAGIC 0x48474332u
#define CRASH_IMAGES 1024
struct crash_image { uintptr_t start, base, end; char path[PATH_MAX]; };
struct crash_record {
    uint32_t magic;
    int32_t signal, code;
    uintptr_t fault, pc, lr, sp, fp, regs[29], base;
    char image[PATH_MAX];
    char missing[256];
};
static struct crash_image g_images[CRASH_IMAGES];
static atomic_uint g_nimages;
static int g_fd = -1;
static _Thread_local const char *t_missing;
static const int k_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};

void hr_crash_register(const henge_image *im)
{
    /* Caller holds the linker lock; published entries are never changed or unloaded. */
    unsigned n = atomic_load_explicit(&g_nimages, memory_order_relaxed);
    if (n == CRASH_IMAGES) return;
    g_images[n].base = im->base;
    g_images[n].start = im->macho_base;
    g_images[n].end = im->end;
    snprintf(g_images[n].path, sizeof g_images[n].path, "%s", im->path);
    atomic_store_explicit(&g_nimages, n + 1, memory_order_release);
}

void hr_crash_missing(const char *name) { t_missing = name; }

static void record_crash(int sig, siginfo_t *info, void *context)
{
    struct crash_record r = {.magic = CRASH_MAGIC, .signal = sig,
                             .code = info->si_code, .fault = (uintptr_t)info->si_addr};
#if defined(__aarch64__)
    ucontext_t *uc = context;
    r.pc = uc->uc_mcontext->__ss.__pc;
    r.lr = uc->uc_mcontext->__ss.__lr;
    r.sp = uc->uc_mcontext->__ss.__sp;
    r.fp = uc->uc_mcontext->__ss.__fp;
    for (unsigned i = 0; i < 29; i++) r.regs[i] = uc->uc_mcontext->__ss.__x[i];
#else
    (void)context;
#endif
    unsigned n = atomic_load_explicit(&g_nimages, memory_order_acquire);
    for (unsigned i = 0; i < n; i++) {
        uintptr_t location = t_missing ? (uintptr_t)t_missing : r.pc;
        if (location < g_images[i].start || location >= g_images[i].end) continue;
        r.base = g_images[i].base;
        for (size_t k = 0; k < sizeof r.image; k++) r.image[k] = g_images[i].path[k];
        break;
    }
    if (t_missing) {
        for (size_t i = 0; i + 1 < sizeof r.missing && t_missing[i]; i++) r.missing[i] = t_missing[i];
    }
    if (g_fd >= 0) {
        const char *p = (const char *)&r;
        size_t left = sizeof r;
        while (left) {
            ssize_t written = write(g_fd, p, left);
            if (written <= 0) { if (written < 0 && errno == EINTR) continue; break; }
            p += written;
            left -= (size_t)written;
        }
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(sig, &action, NULL);
    kill(getpid(), sig); /* delivered after this handler returns; retain the OS crash report */
}

int henge_crash_install(const char *path, char *err, size_t errlen)
{
    if (g_fd >= 0) return hr_fail(err, errlen, "crash recorder already installed");
    if (!path) return hr_fail(err, errlen, "crash record path is NULL");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return hr_fail(err, errlen, "open crash record: %s", strerror(errno));
    struct sigaction action = {0}, previous[sizeof k_signals / sizeof k_signals[0]];
    action.sa_sigaction = record_crash;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof k_signals / sizeof k_signals[0]; i++) sigaddset(&action.sa_mask, k_signals[i]);
    g_fd = fd;
    for (size_t i = 0; i < sizeof k_signals / sizeof k_signals[0]; i++) {
        if (sigaction(k_signals[i], &action, &previous[i]) == 0) continue;
        int saved = errno;
        while (i) { i--; sigaction(k_signals[i], &previous[i], NULL); }
        g_fd = -1;
        close(fd);
        return hr_fail(err, errlen, "install crash recorder: %s", strerror(saved));
    }
    return 0;
}

int henge_crash_read(const char *path, char *report, size_t len)
{
    if (!path || !report || !len) return -1;
    report[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct crash_record r;
    size_t n = 0;
    while (n < sizeof r) {
        ssize_t count = read(fd, (char *)&r + n, sizeof r - n);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { close(fd); return -1; }
        if (!count) break;
        n += (size_t)count;
    }
    close(fd);
    if (!n) return 0; /* a clean run leaves an empty file */
    if (n != sizeof r || r.magic != CRASH_MAGIC) return -1;
    r.image[sizeof r.image - 1] = 0;
    r.missing[sizeof r.missing - 1] = 0;
    int written = snprintf(report, len,
        "Previous run crashed: signal %d (code %d)\n%s + 0x%llx\nPC 0x%llx LR 0x%llx SP 0x%llx FP 0x%llx fault 0x%llx\n",
        r.signal, r.code, r.image[0] ? r.image : "host/unknown image",
        (unsigned long long)(r.pc - r.base), (unsigned long long)r.pc,
        (unsigned long long)r.lr, (unsigned long long)r.sp, (unsigned long long)r.fp,
        (unsigned long long)r.fault);
    if (written >= 0 && (size_t)written < len && r.missing[0])
        written += snprintf(report + written, len - (size_t)written, "Unsupported native function: %s\n", r.missing);
    for (unsigned i = 0; written >= 0 && (size_t)written < len && i < 29; i++) {
        int k = snprintf(report + written, len - (size_t)written, "x%u 0x%llx%s", i,
                         (unsigned long long)r.regs[i], i % 3 == 2 ? "\n" : "  ");
        if (k < 0) return -1;
        written += k;
    }
    if (written < 0 || (size_t)written >= len) return -1;
    unlink(path);
    return 1;
}
