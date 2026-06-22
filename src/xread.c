/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <err.h>
#include "tarpm.h"

/*
 * Wrapper for read(2)
 */
bool
xread(int fd, void *buf, size_t count)
{
    char *readbuf = (char *) buf;
    ssize_t c = count;
    ssize_t total = 0;
    ssize_t n = 0;

    while (total < c) {
        n = read(fd, readbuf + total, c - total);

        if (n == -1) {
            warn("read");
            return false;
        }

        if (n == 0) {
            /* EOF */
            return true;
        }

        total += n;
    }

    return true;
}
