/* DEX file access. Input is untrusted-ish (the app's own APK) but still bounds-checked at the
 * table level; the interpreter trusts verified-looking bytecode. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t dex_uleb(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    uint32_t r = 0;
    int shift = 0;
    for (;;) {
        uint8_t b = *p++;
        r |= (uint32_t)(b & 0x7f) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
    }
    *pp = p;
    return r;
}

int32_t dex_sleb(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int32_t r = 0;
    int shift = 0;
    uint8_t b;
    do {
        b = *p++;
        r |= (int32_t)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 32 && (b & 0x40))
        r |= -(1 << shift);
    *pp = p;
    return r;
}

int dex_open(Dex *dex, const uint8_t *data, size_t size, char *err, size_t errlen)
{
    memset(dex, 0, sizeof *dex);
    if (size < 0x70 || memcmp(data, "dex\n", 4) != 0) {
        snprintf(err, errlen, "not a dex file");
        return -1;
    }
    dex->data = data;
    dex->size = size;
    uint32_t off;
    dex->nstrings = rd32(data + 0x38); off = rd32(data + 0x3c);
    if ((uint64_t)off + (uint64_t)dex->nstrings * 4 > size) { snprintf(err, errlen, "bad string table"); return -1; }
    dex->string_ids = data + off;
    dex->ntypes = rd32(data + 0x40); off = rd32(data + 0x44);
    if ((uint64_t)off + (uint64_t)dex->ntypes * 4 > size) { snprintf(err, errlen, "bad type table"); return -1; }
    dex->type_ids = data + off;
    dex->nprotos = rd32(data + 0x48); off = rd32(data + 0x4c);
    if ((uint64_t)off + (uint64_t)dex->nprotos * 12 > size) { snprintf(err, errlen, "bad proto table"); return -1; }
    dex->proto_ids = data + off;
    dex->nfields = rd32(data + 0x50); off = rd32(data + 0x54);
    if ((uint64_t)off + (uint64_t)dex->nfields * 8 > size) { snprintf(err, errlen, "bad field table"); return -1; }
    dex->field_ids = data + off;
    dex->nmethods = rd32(data + 0x58); off = rd32(data + 0x5c);
    if ((uint64_t)off + (uint64_t)dex->nmethods * 8 > size) { snprintf(err, errlen, "bad method table"); return -1; }
    dex->method_ids = data + off;
    dex->nclasses = rd32(data + 0x60); off = rd32(data + 0x64);
    if ((uint64_t)off + (uint64_t)dex->nclasses * 32 > size) { snprintf(err, errlen, "bad class table"); return -1; }
    dex->class_defs = data + off;
    dex->strings = calloc(dex->nstrings ? dex->nstrings : 1, sizeof *dex->strings);
    dex->classes_by_type = calloc(dex->ntypes ? dex->ntypes : 1, sizeof *dex->classes_by_type);
    dex->res_methods = calloc(dex->nmethods ? dex->nmethods : 1, sizeof *dex->res_methods);
    dex->msigs = calloc(dex->nmethods ? dex->nmethods : 1, sizeof *dex->msigs);
    dex->res_fields = calloc(dex->nfields ? dex->nfields : 1, sizeof *dex->res_fields);
    dex->res_strings = calloc(dex->nstrings ? dex->nstrings : 1, sizeof *dex->res_strings);
    if (!dex->strings || !dex->classes_by_type || !dex->res_methods || !dex->msigs || !dex->res_fields || !dex->res_strings) {
        snprintf(err, errlen, "out of memory");
        dex_close(dex);
        return -1;
    }
    return 0;
}

void dex_close(Dex *dex)
{
    if (!dex)
        return;
    free(dex->strings);
    free(dex->classes_by_type);
    free(dex->res_methods);
    free(dex->msigs);
    free(dex->res_fields);
    free(dex->res_strings);
    memset(dex, 0, sizeof *dex);
}

const char *dex_string(Dex *dex, uint32_t idx)
{
    if (idx >= dex->nstrings)
        return "";
    if (!dex->strings[idx]) {
        const uint8_t *p = dex->data + rd32(dex->string_ids + idx * 4);
        dex_uleb(&p); /* UTF-16 length */
        dex->strings[idx] = (const char *)p;
    }
    return dex->strings[idx];
}

const char *dex_type(Dex *dex, uint32_t type_idx)
{
    if (type_idx >= dex->ntypes)
        return "";
    return dex_string(dex, rd32(dex->type_ids + type_idx * 4));
}
