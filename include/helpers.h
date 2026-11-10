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
#include <archive.h>

/* Macros */
#define xasprintf(dest, ...) {                         \
    *(dest) = NULL;                                    \
    if (asprintf((dest), __VA_ARGS__) == -1) {         \
        err(EXIT_FAILURE, "asprintf");                 \
    }                                                  \
}

/* json-c compatibility */
#if JSON_C_VERSION_NUM < 3584
/* the uint functions appeared in release 0.14.0 */
#define json_object_get_uint64 json_object_get_int64
#endif

#endif /* _TARPM_HELPERS_H */
