/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "tarpm.h"

/*
 * Open and read the contents of a file in to a single buffer and
 * return it.  Caller must free the returned buffer.
 */
void *
read_file_bytes(const char *path, off_t *len)
{
    int r = 0;
    int fd = 0;
    void *data = NULL;
    void *buf = NULL;
    struct stat sb;

    assert(path != NULL);

    /* unreadable files can be ignored */
    if (stat(path, &sb) == -1) {
        return NULL;
    }

    /* zero length file or not a file, ignore */
    if (sb.st_size == 0 || !S_ISREG(sb.st_mode)) {
        return NULL;
    }

    /* open the file for reading */
    fd = open(path, O_RDONLY);

    if (fd == -1) {
        return NULL;
    }

    /* find the file length */
    *len = lseek(fd, 0, SEEK_END);

    if (*len == -1) {
        warn(_("*** unable to find end of %s"), path);
        r = close(fd);
        assert(r != -1);
        return NULL;
    }

    /* map the contents of the file */
    buf = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);

    if (buf == MAP_FAILED) {
        warn(_("*** unable to read %s"), path);
        r = close(fd);
        assert(r != -1);
        return NULL;
    }

    /* the actual file close we care about */
    if (close(fd) == -1) {
        warn(_("*** unable to close %s"), path);
    }

    /* copy the data in to a buffer for the caller */
    data = xalloc(*len + 1);
    data = memcpy(data, buf, *len);
    assert(data != NULL);

    /* unmap */
    r = munmap(buf, *len);
    assert(r == 0);

    return data;
}
