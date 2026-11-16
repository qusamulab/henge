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
HR_SHIM_TABLE(generated);

#include <stdio.h>
/* The Darwin FILE for a stream pointer from Android code (&__sF[n] for the standard streams). */
FILE *hr_file(void *f);
/* Sets the calling thread's bionic errno. */
void hr_set_errno(int linux_errno);
/* A host call may leave errno untouched on success. Isolate host errno, copying only a new
 * error to bionic TLS, then restore the caller's Darwin errno. */
int hr_errno_begin(void);
void hr_errno_end(int saved);

/* printf-style formatting of arguments passed the AAPCS64 variadic way (shim_libc.c): ints are the
 * integer-register arguments after the named ones, fps the eight d registers, stack the first
 * 12 stack slots. n == SIZE_MAX formats without a size limit. */
int hr_format(char *buf, size_t n, const char *fmt, const uint64_t *ints, size_t nints, const double *fps,
              const uint64_t *stack);

/* Returns the shim implementation of the bionic symbol `name`, or NULL. */
void *henge_shim_lookup(const char *name);
void *hr_gl_lookup(const char *name);

#endif
