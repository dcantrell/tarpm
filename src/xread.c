/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <err.h>
#include "tarpm.h"

/*
 * Wrapper for read(2).  NOTE: Passing in a count of 0 is valid and
 * will return true without calling read(2).  We know this.
 */
bool
xread(int fd, void *buf, size_t count)
{
    char *readbuf = (char *) buf;
    ssize_t c = count;
    ssize_t total = 0;
    ssize_t n = 0;

    if (buf == NULL) {
        return false;
    }

    while (total < c) {
        n = read(fd, readbuf + total, c - total);

        if (n == -1) {
            warn("read");
            return false;
        }

        if (n == 0) {
            /* EOF */
            if (total < c) {
                return false;
            } else {
                return true;
            }
        }

        total += n;
    }

    return true;
}
