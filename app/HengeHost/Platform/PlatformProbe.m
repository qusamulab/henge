// Device probes for roadmap items V1 (TPIDR_EL0), V2 (x18) and V6 (memory limit).
// Run with `-probe` at launch; results go to stdout as PASS/INFO lines.
#import <Foundation/Foundation.h>
#import <os/proc.h>
#import <pthread.h>
#import <signal.h>
#import <stdatomic.h>
#import <unistd.h>

static atomic_int g_sig;
static void on_sig(int s) { (void)s; atomic_fetch_add(&g_sig, 1); }

static uint64_t read_tpidr(void) { uint64_t v; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(v)); return v; }
static void write_tpidr(uint64_t v) { __asm__ volatile("msr tpidr_el0, %0" ::"r"(v)); }

/* Writes `magic` to x18, runs `body`, returns what x18 holds afterwards. x18 is the platform
 * register on Darwin, so the compiler never touches it; the asm only saves/restores it. */
#define X18_ROUNDTRIP(magic, body, out)                                   \
    do {                                                                  \
        uint64_t saved_, got_;                                            \
        __asm__ volatile("mov %0, x18" : "=r"(saved_));                   \
        __asm__ volatile("mov x18, %0" ::"r"((uint64_t)(magic)));         \
        body;                                                             \
        __asm__ volatile("mov %0, x18" : "=r"(got_));                     \
        __asm__ volatile("mov x18, %0" ::"r"(saved_));                    \
        (out) = got_;                                                     \
    } while (0)

static int check(const char *name, int ok, const char *fmt, ...) NS_FORMAT_FUNCTION(3, 4);
static int check(const char *name, int ok, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    NSString *d = [[NSString alloc] initWithFormat:[NSString stringWithUTF8String:fmt] arguments:ap];
    va_end(ap);
    printf("%s  %s %s\n", ok ? "PASS" : "FAIL", name, d.UTF8String);
    return !ok;
}

static void *tpidr_thread(void *arg) {
    uint64_t mine = 0xA5A50000u + (uintptr_t)arg;
    write_tpidr(mine);
    for (int i = 0; i < 200; i++) usleep(500);  /* many context switches / migrations */
    uint64_t *r = malloc(sizeof *r);
    *r = read_tpidr() == mine ? 1 : read_tpidr();
    return r;
}

static void *x18_thread(void *arg) {
    uint64_t got;
    X18_ROUNDTRIP(0x1818000000000000ull + (uintptr_t)arg, { for (int i = 0; i < 200; i++) usleep(500); }, got);
    uint64_t *r = malloc(sizeof *r);
    *r = got;
    return r;
}

int henge_run_probes(void) {
    int fails = 0;
    signal(SIGUSR1, on_sig);

    // V1: is TPIDR_EL0 free, and is it preserved across context switches and signals?
    uint64_t orig = read_tpidr();
    printf("INFO  V1 tpidr_el0 initial = 0x%llx\n", orig);
    write_tpidr(0xDEADBEEF00ull);
    int equal_immediately = read_tpidr() == 0xDEADBEEF00ull;
    usleep(100000);
    int after_sleep = read_tpidr() == 0xDEADBEEF00ull;
    atomic_store(&g_sig, 0);
    raise(SIGUSR1);
    int sig_ran = atomic_load(&g_sig) == 1;
    int after_sig = read_tpidr() == 0xDEADBEEF00ull;
    printf("INFO  V1 write sticks=%d after sleep=%d signal ran=%d after signal=%d now=0x%llx\n",
           equal_immediately, after_sleep, sig_ran, after_sig, read_tpidr());
    pthread_t th[4]; int ok = 1;
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, tpidr_thread, (void *)(uintptr_t)i);
    for (int i = 0; i < 4; i++) { uint64_t *r; pthread_join(th[i], (void **)&r); if (*r != 1) { ok = 0; printf("INFO  V1 thread %d read 0x%llx\n", i, *r); } free(r); }
    write_tpidr(orig);
    fails += check("V1 tpidr_el0 survives sleep+signal+threads", after_sleep && after_sig && ok, "(free=%s)",
                   (after_sleep && after_sig && ok) ? "yes" : "no");

    // V2: does XNU preserve x18?
    uint64_t got = 0;
    X18_ROUNDTRIP(0x1818181818181818ull, { usleep(100000); }, got);
    int sleep_ok = got == 0x1818181818181818ull;
    atomic_store(&g_sig, 0);
    X18_ROUNDTRIP(0x1818181818181818ull, { raise(SIGUSR1); }, got);
    int sig_ok = got == 0x1818181818181818ull;
    X18_ROUNDTRIP(0x1818181818181818ull, { getpid(); sched_yield(); }, got);
    int sys_ok = got == 0x1818181818181818ull;
    int thr_ok = 1;
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, x18_thread, (void *)(uintptr_t)i);
    for (int i = 0; i < 4; i++) { uint64_t *r; pthread_join(th[i], (void **)&r); if (*r != 0x1818000000000000ull + i) { thr_ok = 0; printf("INFO  V2 thread %d x18=0x%llx\n", i, *r); } free(r); }
    printf("INFO  V2 x18 after sleep=%d signal=%d syscall=%d threads=%d (last=0x%llx)\n", sleep_ok, sig_ok, sys_ok, thr_ok, got);
    fails += check("V2 x18 preserved", sleep_ok && sig_ok && sys_ok && thr_ok, "");

    // V6: memory budget
    printf("INFO  V6 os_proc_available_memory = %zu MiB, physical = %llu MiB\n",
           os_proc_available_memory() >> 20, [NSProcessInfo processInfo].physicalMemory >> 20);
    printf("%s: probes (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}
