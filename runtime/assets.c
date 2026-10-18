/* AAssetManager over an APK: assets/<name> entries, read through the apk/ reader. Files are
 * inflated into memory when opened (games read their assets whole or stream from the buffer). */
#include "android.h"
#include "shim.h"
#include "henge/apk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct AAssetManager {
    henge_apk *apk;
};

struct AAsset {
    uint8_t *buf;
    size_t len, pos;
};

struct AAssetDir {
    char **names;
    size_t n, pos;
};

struct AAssetManager *hr_asset_manager_new(const char *apk_path)
{
    struct AAssetManager *m = calloc(1, sizeof *m);
    char err[256];
    if (!m)
        return NULL;
    m->apk = henge_apk_open(apk_path, err, sizeof err);
    if (!m->apk) {
        free(m);
        return NULL;
    }
    return m;
}

void hr_asset_manager_free(struct AAssetManager *m)
{
    if (!m)
        return;
    henge_apk_close(m->apk);
    free(m);
}

static struct AAssetManager *g_default_manager;

void hr_asset_manager_set_default(struct AAssetManager *m)
{
    g_default_manager = m;
}

/* AAssetManager_fromJava(env, assetManagerObj): the JNI stub has no Java objects, so hand out the
 * running activity's manager. */
static struct AAssetManager *shim_from_java(void *env, void *obj)
{
    (void)env;
    (void)obj;
    return g_default_manager;
}

static struct AAsset *shim_asset_open(struct AAssetManager *m, const char *filename, int mode)
{
    (void)mode; /* UNKNOWN / RANDOM / STREAMING / BUFFER: all served from memory */
    if (!m || !filename)
        return NULL;
    char name[1024];
    snprintf(name, sizeof name, "assets/%s", filename);
    long i = henge_apk_find(m->apk, name);
    if (i < 0)
        return NULL;
    struct AAsset *a = calloc(1, sizeof *a);
    char err[256];
    if (!a)
        return NULL;
    if (henge_apk_extract(m->apk, (size_t)i, &a->buf, &a->len, err, sizeof err) != 0) {
        free(a);
        return NULL;
    }
    return a;
}

static void shim_asset_close(struct AAsset *a)
{
    if (a) {
        free(a->buf);
        free(a);
    }
}

static int shim_asset_read(struct AAsset *a, void *buf, size_t count)
{
    size_t n = a->len - a->pos < count ? a->len - a->pos : count;
    memcpy(buf, a->buf + a->pos, n);
    a->pos += n;
    return (int)n;
}

static off_t shim_asset_seek(struct AAsset *a, off_t offset, int whence)
{
    off_t base = whence == 1 ? (off_t)a->pos : whence == 2 ? (off_t)a->len : 0;
    off_t to = base + offset;
    if (whence < 0 || whence > 2 || to < 0 || to > (off_t)a->len)
        return -1;
    a->pos = (size_t)to;
    return to;
}

static off_t shim_asset_length(struct AAsset *a) { return (off_t)a->len; }
static off_t shim_asset_remaining(struct AAsset *a) { return (off_t)(a->len - a->pos); }
static const void *shim_asset_buffer(struct AAsset *a) { return a->buf; }
static int shim_asset_is_allocated(struct AAsset *a) { (void)a; return 0; }

static int shim_asset_fd(struct AAsset *a, off_t *start, off_t *length)
{
    (void)a;
    (void)start;
    (void)length;
    return -1; /* not backed by a file descriptor */
}

static struct AAssetDir *shim_open_dir(struct AAssetManager *m, const char *dir)
{
    if (!m)
        return NULL;
    char prefix[1024];
    snprintf(prefix, sizeof prefix, "assets/%s%s", dir, *dir && dir[strlen(dir) - 1] != '/' ? "/" : "");
    size_t plen = strlen(prefix);
    struct AAssetDir *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    for (size_t i = 0; i < henge_apk_count(m->apk); i++) {
        const char *name = henge_apk_name(m->apk, i);
        if (!name || strncmp(name, prefix, plen) != 0 || !name[plen] || strchr(name + plen, '/'))
            continue; /* not in this directory, a directory entry, or in a subdirectory */
        char **n = realloc(d->names, (d->n + 1) * sizeof *n);
        if (!n)
            break;
        d->names = n;
        d->names[d->n++] = strdup(name + plen);
    }
    return d;
}

static const char *shim_dir_next(struct AAssetDir *d)
{
    return d->pos < d->n ? d->names[d->pos++] : NULL;
}

static void shim_dir_rewind(struct AAssetDir *d) { d->pos = 0; }

static void shim_dir_close(struct AAssetDir *d)
{
    if (!d)
        return;
    for (size_t i = 0; i < d->n; i++)
        free(d->names[i]);
    free(d->names);
    free(d);
}

#define E HR_SHIM
const struct hr_shim_entry hr_shim_assets[] = {
    E("AAssetManager_fromJava", shim_from_java),
    E("AAssetManager_open", shim_asset_open),
    E("AAssetManager_openDir", shim_open_dir),
    E("AAsset_close", shim_asset_close),
    E("AAsset_read", shim_asset_read),
    E("AAsset_seek", shim_asset_seek),
    E("AAsset_seek64", shim_asset_seek),
    E("AAsset_getLength", shim_asset_length),
    E("AAsset_getLength64", shim_asset_length),
    E("AAsset_getRemainingLength", shim_asset_remaining),
    E("AAsset_getRemainingLength64", shim_asset_remaining),
    E("AAsset_getBuffer", shim_asset_buffer),
    E("AAsset_isAllocated", shim_asset_is_allocated),
    E("AAsset_openFileDescriptor", shim_asset_fd),
    E("AAsset_openFileDescriptor64", shim_asset_fd),
    E("AAssetDir_getNextFileName", shim_dir_next),
    E("AAssetDir_rewind", shim_dir_rewind),
    E("AAssetDir_close", shim_dir_close),
};
#undef E
const size_t hr_shim_assets_count = sizeof hr_shim_assets / sizeof hr_shim_assets[0];
