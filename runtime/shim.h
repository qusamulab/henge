/* bionic compatibility shim. See docs/architecture.md §6 and research/abi-differences.md.
 *
 * Each shim_*.c / dl.c file exports a table of {bionic name, implementation}; shim.c merges
 * them into one sorted index. A name must appear in exactly one table. */
#ifndef HENGE_SHIM_H
#define HENGE_SHIM_H

#include <stddef.h>
#include <stdint.h>

struct hr_shim_entry {
    const char *name;
    void *addr;
};

#define HR_SHIM(n, f) {n, (void *)(f)}
#define HR_SHIM_TABLE(id) \
    extern const struct hr_shim_entry hr_shim_##id[]; \
    extern const size_t hr_shim_##id##_count

HR_SHIM_TABLE(core);
HR_SHIM_TABLE(dl);
HR_SHIM_TABLE(posix);
HR_SHIM_TABLE(android);
HR_SHIM_TABLE(assets);
HR_SHIM_TABLE(libc);
HR_SHIM_TABLE(cxx);

#include <stdio.h>
/* The Darwin FILE for a stream pointer from Android code (&__sF[n] for the standard streams). */
FILE *hr_file(void *f);
/* Sets the calling thread's bionic errno. */
void hr_set_errno(int linux_errno);

/* printf-style formatting of arguments passed the AAPCS64 variadic way (shim_libc.c): ints are the
 * integer-register arguments after the named ones, fps the eight d registers, stack the first
 * 12 stack slots. n == 0 formats without a size limit. */
int hr_format(char *buf, size_t n, const char *fmt, const uint64_t *ints, size_t nints, const double *fps,
              const uint64_t *stack);

/* Returns the shim implementation of the bionic symbol `name`, or NULL. */
void *henge_shim_lookup(const char *name);

#endif
