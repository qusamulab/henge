#define _DARWIN_C_SOURCE
#include "henge/henge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); *size = (size_t)ftell(f); rewind(f);
    uint8_t *data = malloc(*size + 1); assert(data);
    assert(fread(data, 1, *size, f) == *size); fclose(f); return data;
}
static void equal(const char *path, const uint8_t *bytes, size_t size)
{
    size_t n; uint8_t *data = read_file(path, &n);
    assert(n == size && !memcmp(data, bytes, size)); free(data);
}
static int prepare(const char *input, char *output, size_t capacity, void *user, char *err, size_t errlen)
{
    snprintf(output, capacity, "%s", (const char *)user);
    char temp[4096]; snprintf(temp, sizeof temp, "%s.unsigned", output);
    struct henge_convert_opts opts = {HENGE_PLATFORM_MACOS, "@rpath/dynamic.dylib", 0};
    struct henge_convert_result result;
    int rc = henge_convert_file(input, temp, &opts, &result, err, errlen);
    henge_convert_result_free(&result);
    if (!rc) rc = henge_sign_file(temp, output, "dynamic", NULL, err, errlen);
    unlink(temp); return rc;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    char directory[] = "/tmp/henge-files-XXXXXX"; assert(mkdtemp(directory));
    char converted[4096], signed_path[4096];
    snprintf(converted, sizeof converted, "%s/output.dylib", directory);
    snprintf(signed_path, sizeof signed_path, "%s/signed.dylib", directory);
    size_t size; uint8_t *elf = read_file(argv[1], &size);
    struct henge_convert_opts opts = {HENGE_PLATFORM_MACOS, "@rpath/test.dylib", 0};
    struct henge_convert_result memory, file;
    char err[512];
    assert(!henge_convert(elf, size, &opts, &memory, err, sizeof err));
    assert(!henge_convert_file(argv[1], converted, &opts, &file, err, sizeof err));
    assert(!file.macho && file.macho_size == memory.macho_size);
    assert(!strcmp(memory.manifest_json, file.manifest_json));
    equal(converted, memory.macho, memory.macho_size);
    uint8_t *signed_bytes; size_t signed_size;
    assert(!henge_sign_adhoc(memory.macho, memory.macho_size, "test", &signed_bytes, &signed_size, err, sizeof err));
    assert(!henge_sign_file(converted, signed_path, "test", NULL, err, sizeof err));
    equal(signed_path, signed_bytes, signed_size);
    /* A failed conversion/sign leaves the previously valid destination byte-for-byte intact. */
    struct henge_convert_result failed;
    assert(henge_convert_file(signed_path, converted, &opts, &failed, err, sizeof err));
    equal(converted, memory.macho, memory.macho_size);
    assert(henge_sign_file(argv[1], signed_path, "test", NULL, err, sizeof err));
    equal(signed_path, signed_bytes, signed_size);
    /* Also cover in-place signing without destroying the mapped input. */
    assert(!henge_sign_file(converted, converted, "test", NULL, err, sizeof err));
    equal(converted, signed_bytes, signed_size);
    henge_set_elf_prepare(prepare, signed_path);
    henge_image *im = henge_load(argv[1], err, sizeof err);
    if (!im) fprintf(stderr, "%s\n", err);
    assert(im);
    int (*add)(int, int) = henge_dlsym(im, "add"); assert(add && add(2, 3) == 5);
    char second[4096]; snprintf(second, sizeof second, "%s/second.dylib", directory);
    henge_set_elf_prepare(prepare, second);
    im = henge_load(argv[2], err, sizeof err);
    if (!im) fprintf(stderr, "%s\n", err);
    assert(im);
    int (*unused)(void) = henge_dlsym(im, "unused_missing"); assert(unused && unused() == 42);
    henge_convert_result_free(&memory); henge_convert_result_free(&file);
    free(elf); free(signed_bytes); unlink(converted); unlink(signed_path); unlink(second); rmdir(directory);
    puts("PASS: test_files (equivalence, atomic failure, in-place signing, on-demand ELF loading)");
    return 0;
}
