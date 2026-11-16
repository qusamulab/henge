/* Core C-runtime adapters and the merged shim index. ABI-compatible fixed signatures and
 * errno wrappers are generated from scripts/libc-api.json; ABI/struct/constant conversions
 * stay in handwritten adapters. Review research/abi-differences.md before adding an export. */
#define _DARWIN_C_SOURCE
#include "shim.h"
#include "henge/henge.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
/* Darwin's own __cxa_atexit (libSystem). */
extern int __cxa_atexit(void (*func)(void *), void *arg, void *dso);
static void shim_cxa_finalize(void *dso)
{
    /* No-op in M1: images are never unloaded. */
    (void)dso;
}
static int shim_cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
    /* dso is &__dso_handle inside the converted image, i.e. an address in the
     * mapped dylib, so Darwin's per-image finalization still matches it. */
    return __cxa_atexit(func, arg, dso);
}
static int shim_register_atfork(void (*prepare)(void), void (*parent)(void),
                                void (*child)(void), void *dso)
{
    /* bionic's pthread_atfork() is a wrapper passing &__dso_handle. */
    (void)dso;
    return pthread_atfork(prepare, parent, child);
}
static void shim_stack_chk_fail(void)
{
    abort();
}
#define E HR_SHIM
const struct hr_shim_entry hr_shim_core[] = {
    E("__cxa_finalize", shim_cxa_finalize),
    E("__cxa_atexit", shim_cxa_atexit),
    E("__register_atfork", shim_register_atfork),
    E("__stack_chk_fail", shim_stack_chk_fail),
};
#undef E
const size_t hr_shim_core_count = sizeof hr_shim_core / sizeof hr_shim_core[0];
/* ---- merged, sorted index ---- */
static const struct {
    const struct hr_shim_entry *entries;
    const size_t *count;
} k_tables[] = {
    {hr_shim_core, &hr_shim_core_count},
    {hr_shim_dl, &hr_shim_dl_count},
    {hr_shim_posix, &hr_shim_posix_count},
    {hr_shim_android, &hr_shim_android_count},
    {hr_shim_assets, &hr_shim_assets_count},
    {hr_shim_libc, &hr_shim_libc_count},
    {hr_shim_cxx, &hr_shim_cxx_count},
    {hr_shim_generated, &hr_shim_generated_count},
};
/* Tables added at run time by the host (henge_shim_register). */
#define MAX_DYNAMIC_TABLES 8
static struct {
    const struct henge_shim_entry *entries;
    size_t count;
} g_dynamic[MAX_DYNAMIC_TABLES];
static size_t g_ndynamic;
static pthread_mutex_t g_index_lock = PTHREAD_MUTEX_INITIALIZER;
static const struct hr_shim_entry **g_index;
static size_t g_nindex;
static int g_index_valid;
static int cmp_entry(const void *a, const void *b)
{
    return strcmp((*(const struct hr_shim_entry *const *)a)->name,
                  (*(const struct hr_shim_entry *const *)b)->name);
}
/* Built-in names are unique. Caller holds g_index_lock. */
static void build_index(void)
{
    size_t n = 0;
    for (size_t t = 0; t < sizeof k_tables / sizeof k_tables[0]; t++)
        n += *k_tables[t].count;
    free(g_index);
    g_index = malloc(n * sizeof *g_index);
    if (!g_index)
        abort();
    g_nindex = 0;
    for (size_t t = 0; t < sizeof k_tables / sizeof k_tables[0]; t++)
        for (size_t i = 0; i < *k_tables[t].count; i++)
            g_index[g_nindex++] = &k_tables[t].entries[i];
    qsort(g_index, g_nindex, sizeof *g_index, cmp_entry);
    g_index_valid = 1;
}
void henge_shim_register(const struct henge_shim_entry *entries, size_t count)
{
    pthread_mutex_lock(&g_index_lock);
    if (g_ndynamic < MAX_DYNAMIC_TABLES) {
        g_dynamic[g_ndynamic].entries = entries;
        g_dynamic[g_ndynamic].count = count;
        g_ndynamic++;
        g_index_valid = 0;
    }
    pthread_mutex_unlock(&g_index_lock);
}
static void *lookup_index(const char *name)
{
    size_t lo = 0, hi = g_nindex;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(name, g_index[mid]->name);
        if (c == 0)
            return g_index[mid]->addr;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}
void *henge_shim_lookup(const char *name)
{
    pthread_mutex_lock(&g_index_lock);
    if (!g_index_valid)
        build_index();
    void *p = NULL;
    /* Newest platform overrides win deterministically, including over built-in names. */
    for (size_t t = g_ndynamic; t && !p; t--)
        for (size_t i = 0; i < g_dynamic[t - 1].count; i++)
            if (!strcmp(name, g_dynamic[t - 1].entries[i].name)) {
                p = g_dynamic[t - 1].entries[i].addr; break;
            }
    if (!p) p = lookup_index(name);
    pthread_mutex_unlock(&g_index_lock);
    if (p)
        return p;
    /* Only reviewed GLES signatures enter the generated Android/Darwin boundary. */
    if (name[0] == 'g' && name[1] == 'l' && name[2] >= 'A' && name[2] <= 'Z')
        return hr_gl_lookup(name);
    return NULL;
}
int henge_shim_provides(const char *name)
{
    return henge_shim_lookup(name) != NULL;
}
