/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_HELPERS_H
#define _TARPM_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>

/* Macros */
#define xasprintf(dest, ...) {                         \
    *(dest) = NULL;                                    \
    if (asprintf((dest), __VA_ARGS__) == -1) {         \
        err(EXIT_FAILURE, "asprintf");                 \
    }                                                  \
}

#endif /* _TARPM_HELPERS_H */
