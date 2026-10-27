/* APK lifetime, archive-order enumeration and indexed name lookup. */
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int ha_fail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* Sort by name, then archive position, preserving first-entry-wins for duplicates. */
static int compare_names(const void *left, const void *right) {
    const struct apk_entry *a = *(const struct apk_entry *const *)left;
    const struct apk_entry *b = *(const struct apk_entry *const *)right;
    int cmp = strcmp(a->name, b->name);
    return cmp ? cmp : (a > b) - (a < b);
}

/* ---- open / close ---- */

static henge_apk *open_common(henge_apk *a, char *err, size_t errlen) {
    if (ha_read_central_directory(a, err, errlen) != 0) {
        henge_apk_close(a);
        return NULL;
    }
    a->by_name = malloc((a->count ? a->count : 1) * sizeof *a->by_name);
    if (!a->by_name) {
        ha_fail(err, errlen, "out of memory");
        henge_apk_close(a);
        return NULL;
    }
    for (size_t i = 0; i < a->count; i++)
        a->by_name[i] = &a->entries[i];
    qsort(a->by_name, a->count, sizeof *a->by_name, compare_names);
    return a;
}

henge_apk *henge_apk_open_mem(const uint8_t *data, size_t size, char *err, size_t errlen) {
    henge_apk *a = calloc(1, sizeof *a);
    if (!a) {
        ha_fail(err, errlen, "out of memory");
        return NULL;
    }
    a->data = data;
    a->size = size;
    return open_common(a, err, errlen);
}

henge_apk *henge_apk_open(const char *path, char *err, size_t errlen) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ha_fail(err, errlen, "%s: cannot open: %s", path, strerror(errno));
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        ha_fail(err, errlen, "%s: not a non-empty regular file", path);
        return NULL;
    }
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        ha_fail(err, errlen, "%s: mmap failed: %s", path, strerror(errno));
        return NULL;
    }
    henge_apk *a = calloc(1, sizeof *a);
    if (!a) {
        munmap(m, (size_t)st.st_size);
        ha_fail(err, errlen, "out of memory");
        return NULL;
    }
    a->data = m;
    a->size = (size_t)st.st_size;
    a->mapped = 1;
    return open_common(a, err, errlen);
}

void henge_apk_close(henge_apk *a) {
    if (!a) return;
    for (size_t i = 0; i < a->count; i++) free(a->entries[i].name);
    free(a->by_name);
    free(a->entries);
    if (a->mapped) munmap((void *)a->data, a->size);
    free(a);
}

/* ---- lookup ---- */

size_t henge_apk_count(const henge_apk *a) { return a ? a->count : 0; }

const char *henge_apk_name(const henge_apk *a, size_t i) { return a && i < a->count ? a->entries[i].name : NULL; }

long henge_apk_find(const henge_apk *a, const char *name) {
    if (!a || !name) return -1;
    size_t lo = 0, hi = a->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(a->by_name[mid]->name, name) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < a->count && strcmp(a->by_name[lo]->name, name) == 0
         ? (long)(a->by_name[lo] - a->entries) : -1;
}

int henge_apk_native_libs(const henge_apk *a, const char *abi, size_t **idx, size_t *n) {
    *idx = NULL;
    *n = 0;
    char prefix[128];
    if (!a || !abi || snprintf(prefix, sizeof prefix, "lib/%s/", abi) >= (int)sizeof prefix) return 0;
    size_t plen = strlen(prefix), cap = 0;
    for (size_t i = 0; i < a->count; i++) {
        const char *name = a->entries[i].name;
        size_t len = strlen(name);
        if (strncmp(name, prefix, plen) != 0 || strchr(name + plen, '/') || len < plen + 4 ||
            strcmp(name + len - 3, ".so") != 0)
            continue;
        if (*n == cap) {
            cap = cap ? cap * 2 : 8;
            size_t *ni = realloc(*idx, cap * sizeof *ni);
            if (!ni) {
                free(*idx);
                *idx = NULL;
                *n = 0;
                return -1;
            }
            *idx = ni;
        }
        (*idx)[(*n)++] = i;
    }
    return 0;
}
