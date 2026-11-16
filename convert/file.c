/* File-backed conversion/signing: no whole-image malloc for ELF, Mach-O or signed output. */
#define _DARWIN_C_SOURCE
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void *hc_buffer_alloc(size_t size, int fd)
{
    if (fd < 0) return calloc(1, size);
    if (ftruncate(fd, (off_t)size)) return NULL;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return p == MAP_FAILED ? NULL : p;
}

void hc_buffer_free(void *buffer, size_t size, int fd)
{
    if (fd < 0) free(buffer);
    else munmap(buffer, size);
}

int hc_map_input(const char *path, const uint8_t **data, size_t *size, char *err, size_t errlen)
{
    int fd = path ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0) return hc_fail(err, errlen, "open input: %s", path ? strerror(errno) : "NULL path");
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > SIZE_MAX) {
        close(fd);
        return hc_fail(err, errlen, "input must be a nonempty regular file");
    }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return hc_fail(err, errlen, "mmap input: %s", strerror(errno));
    *data = p;
    *size = (size_t)st.st_size;
    return 0;
}

int hc_output_open(const char *path, char **temporary, char *err, size_t errlen)
{
    if (!path) return hc_fail(err, errlen, "NULL output path");
    *temporary = malloc(strlen(path) + 12);
    if (!*temporary) return hc_fail(err, errlen, "out of memory");
    sprintf(*temporary, "%s.XXXXXX", path);
    int fd = mkstemp(*temporary);
    if (fd < 0) {
        free(*temporary);
        *temporary = NULL;
        return hc_fail(err, errlen, "create output: %s", strerror(errno));
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int hc_output_finish(int fd, const char *temporary, const char *path, int success, char *err, size_t errlen)
{
    if (success && fsync(fd)) { success = 0; hc_fail(err, errlen, "sync output: %s", strerror(errno)); }
    if (close(fd) && success) { success = 0; hc_fail(err, errlen, "close output: %s", strerror(errno)); }
    if (success && rename(temporary, path)) { success = 0; hc_fail(err, errlen, "publish output: %s", strerror(errno)); }
    if (!success) unlink(temporary);
    return success ? 0 : -1;
}

int henge_sign_file(const char *input, const char *output, const char *identifier,
                    const struct henge_signer *signer, char *err, size_t errlen)
{
    if (signer && (!signer->cert || !signer->sign || !signer->signature_len))
        return hc_fail(err, errlen, "incomplete signer");
    const uint8_t *data;
    size_t size;
    if (hc_map_input(input, &data, &size, err, errlen)) return -1;
    char *temporary = NULL;
    int fd = hc_output_open(output, &temporary, err, errlen);
    if (fd < 0) { munmap((void *)data, size); return -1; }
    uint8_t *signed_data = NULL;
    size_t signed_size = 0;
    int rc = hc_sign_to_fd(data, size, identifier, signer, fd, &signed_data, &signed_size, err, errlen);
    if (rc == 0) {
        /* Drop dirty shared pages after flushing: Darwin otherwise marks the executable
         * mapping as code-signing tainted even when its on-disk hashes are correct. */
        if (msync(signed_data, signed_size, MS_SYNC | MS_INVALIDATE)) rc = hc_fail(err, errlen, "flush signed image failed");
        munmap(signed_data, signed_size);

    }
    munmap((void *)data, size);
    rc = hc_output_finish(fd, temporary, output, rc == 0, err, errlen);
    free(temporary);
    return rc;
}
