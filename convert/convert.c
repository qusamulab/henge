/* henge-convert library entry point: repack an Android arm64 ELF shared object as a
 * Mach-O dylib. Layout and rules: docs/m1-elf-to-macho.md §3–4.
 *
 *   elf_input.c     parse + validate the ELF (§3)
 *   scan.c          instruction scan (svc / tpidr_el0 / x18), docs/m2-patching.md
 *   dynsym.c        imports from the dynamic symbol table
 *   macho_writer.c  emit the Mach-O (§4) and the patch island
 *   manifest.c      emit the manifest JSON
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach-o/loader.h>

#include "internal.h"

int hc_fail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int convert_impl(const uint8_t *elf, size_t elf_size, const struct henge_convert_opts *opts,
                         int fd, struct henge_convert_result *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);

    if (!opts || !opts->install_name || !*opts->install_name)
        return hc_fail(err, errlen, "install name is required");
    if (opts->platform != HENGE_PLATFORM_IOS && opts->platform != HENGE_PLATFORM_MACOS &&
        opts->platform != HENGE_PLATFORM_IOSSIM)
        return hc_fail(err, errlen, "unknown platform %d", (int)opts->platform);

    struct elf_info e;
    if (hc_elf_parse(&e, elf, elf_size, err, errlen) != 0) return -1;

    struct scan_result scan;
    if (hc_scan_code(&e, &scan, err, errlen) != 0) return -1;
    if (hc_elf_symbols(&e, 0, &out->imports, &out->nimports, err, errlen) != 0 ||
        hc_elf_symbols(&e, 1, &out->exports, &out->nexports, err, errlen) != 0) {
        hc_scan_free(&scan);
        henge_convert_result_free(out);
        return -1;
    }
    out->svc_count = scan.svc_count;
    out->tpidr_count = scan.tpidr_count;
    out->x18_count = scan.x18_count;
    out->shadow_call_stack = scan.scs;
    out->has_tls = e.tls_ph != NULL;

    out->macho_mapped = fd >= 0;
    int rc = hc_macho_write(&e, opts, &scan, out->imports, out->nimports, fd, &out->macho, &out->macho_size, err, errlen);
    if (rc == 0) {
        const struct mach_header_64 *mh = (const void *)out->macho;
        uint64_t shift = 0;
        const uint8_t *p = (const void *)(mh + 1);
        for (unsigned i = 0; i < mh->ncmds; i++) {
            const struct load_command *lc = (const void *)p;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const void *)p;
                const struct section_64 *sections = (const void *)(seg + 1);
                for (unsigned k = 0; k < seg->nsects; k++)
                    if (!strncmp(sections[k].sectname, "__elf", 16)) shift = sections[k].addr;
            }
            p += lc->cmdsize;
        }
        out->manifest_json = hc_manifest_build(&e, opts, &scan, out->imports, out->nimports, shift);
        if (!out->manifest_json) rc = hc_fail(err, errlen, "out of memory");
    }
    hc_scan_free(&scan);
    if (rc != 0) henge_convert_result_free(out);
    return rc;
}

int henge_convert(const uint8_t *elf, size_t size, const struct henge_convert_opts *opts,
                   struct henge_convert_result *out, char *err, size_t errlen) {
    return convert_impl(elf, size, opts, -1, out, err, errlen);
}

int henge_convert_file(const char *input, const char *output, const struct henge_convert_opts *opts,
                       struct henge_convert_result *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    const uint8_t *data;
    size_t size;
    if (hc_map_input(input, &data, &size, err, errlen)) return -1;
    char *temporary = NULL;
    int fd = hc_output_open(output, &temporary, err, errlen);
    if (fd < 0) { munmap((void *)data, size); return -1; }
    int rc = convert_impl(data, size, opts, fd, out, err, errlen);
    if (rc == 0) {
        if (msync(out->macho, out->macho_size, MS_SYNC) != 0) rc = hc_fail(err, errlen, "flush converted image failed");
        munmap(out->macho, out->macho_size);
        out->macho = NULL;
        out->macho_mapped = 0;
    }
    munmap((void *)data, size);
    rc = hc_output_finish(fd, temporary, output, rc == 0, err, errlen);
    free(temporary);
    if (rc) henge_convert_result_free(out);
    return rc;
}

void henge_convert_result_free(struct henge_convert_result *res) {
    if (res->macho) hc_buffer_free(res->macho, res->macho_size, res->macho_mapped ? 0 : -1);
    free(res->manifest_json);
    hc_symbols_free(res->imports, res->nimports);
    hc_symbols_free(res->exports, res->nexports);
    memset(res, 0, sizeof *res);
}
