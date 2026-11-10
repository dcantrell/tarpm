/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "tarpm.h"

/*
 * Open and read the contents of a file in to a single buffer and
 * return it.  Caller must free the returned buffer.
 */
void *
read_file_bytes(const char *path, off_t *len)
{
    int fd = 0;
    void *data = NULL;
    struct stat sb;

    if (path == NULL || len == NULL) {
        return NULL;
    }

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

    if (*len == -1 || lseek(fd, 0, SEEK_SET) == -1) {
        warn(_("*** unable to find end of %s"), path);

        if (close(fd) == -1) {
            warn(_("*** unable to close %s"), path);
        }

        return NULL;
    }

    /*
     * We read() rather than mmap() so a file truncated under us is a
     * short read and not a SIGBUS
     */
    data = xalloc(*len + 1);

    if (!xread(fd, data, *len)) {
        warnx(_("*** unable to read %s"), path);
        free(data);
        data = NULL;
    }

    /* the actual file close we care about */
    if (close(fd) == -1) {
        warn(_("*** unable to close %s"), path);
    }

    return data;
}
