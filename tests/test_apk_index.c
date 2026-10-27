/* Name lookup must retain ZIP archive order and the first occurrence of duplicate names. */
#include "henge/apk.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ENTRIES = 2048, NAME_SIZE = 16, CENTRAL_SIZE = 46, EOCD_SIZE = 22 };

static void put16(uint8_t *p, unsigned n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void put32(uint8_t *p, unsigned n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}

int main(void)
{
    /* Lookup needs only the directory. Extraction of these synthetic entries is never requested. */
    size_t cd_size = (ENTRIES + 1) * (CENTRAL_SIZE + NAME_SIZE);
    size_t size = cd_size + EOCD_SIZE;
    uint8_t *zip = calloc(1, size);
    assert(zip);
    for (unsigned i = 0; i <= ENTRIES; i++) {
        uint8_t *entry = zip + i * (CENTRAL_SIZE + NAME_SIZE);
        char name[NAME_SIZE + 1];
        snprintf(name, sizeof name, "assets/%05u.dat", i == ENTRIES ? ENTRIES / 2 : ENTRIES - i - 1);
        assert(strlen(name) == NAME_SIZE);
        put32(entry, 0x02014b50u);
        put16(entry + 28, NAME_SIZE);
        memcpy(entry + CENTRAL_SIZE, name, NAME_SIZE);
    }
    uint8_t *eocd = zip + cd_size;
    put32(eocd, 0x06054b50u);
    put16(eocd + 8, ENTRIES + 1);
    put16(eocd + 10, ENTRIES + 1);
    put32(eocd + 12, (unsigned)cd_size);

    char err[256] = "";
    henge_apk *apk = henge_apk_open_mem(zip, size, err, sizeof err);
    assert(apk && henge_apk_count(apk) == ENTRIES + 1);
    for (unsigned i = 0; i < ENTRIES; i++) {
        char name[NAME_SIZE + 1];
        snprintf(name, sizeof name, "assets/%05u.dat", i);
        long index = henge_apk_find(apk, name);
        assert(index == ENTRIES - i - 1);
        assert(strcmp(henge_apk_name(apk, (size_t)index), name) == 0);
    }
    assert(henge_apk_find(apk, "") == -1);
    assert(henge_apk_find(apk, "assets/01024.da") == -1);
    assert(henge_apk_find(apk, "assets/01024.datx") == -1);
    assert(henge_apk_find(apk, "z") == -1);
    henge_apk_close(apk);

    /* Empty ZIPs also have a valid, empty index. */
    put16(eocd + 8, 0);
    put16(eocd + 10, 0);
    put32(eocd + 12, 0);
    apk = henge_apk_open_mem(eocd, EOCD_SIZE, err, sizeof err);
    assert(apk && henge_apk_count(apk) == 0);
    assert(henge_apk_find(apk, "anything") == -1);
    henge_apk_close(apk);
    free(zip);
    puts("PASS: test_apk_index (2049 entries, duplicates, missing names and empty ZIP)");
    return 0;
}
