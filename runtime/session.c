/* A loaded guest cannot be reset: dyld mappings, ELF globals and native threads survive Stop. */
#include "internal.h"
#include <stdatomic.h>

static atomic_int g_started;

int henge_guest_started(void) { return atomic_load(&g_started); }

int henge_guest_begin(char *err, size_t errlen)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_started, &expected, 1))
        return hr_fail(err, errlen, "Restart henge before running another APK. A guest was already loaded in this process.");
    hr_lock();
    int loaded = hr_images != NULL;
    hr_unlock();
    if (loaded) return hr_fail(err, errlen, "Restart henge before running an APK: native diagnostics already loaded code in this process.");
    return 0;
}
