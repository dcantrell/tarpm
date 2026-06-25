/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_HELPERS_H
#define _TARPM_HELPERS_H

#include <stdio.h>
#include <assert.h>

/* Macros */
#ifdef NDEBUG
/* Don't create unused variables if not using assert() */
#define xasprintf(dest, ...) {                         \
    *(dest) = NULL;                                    \
    asprintf((dest), __VA_ARGS__);                     \
}
#else
#define xasprintf(dest, ...) {                         \
    int _xasprintf_result;                             \
    *(dest) = NULL;                                    \
    _xasprintf_result = asprintf((dest), __VA_ARGS__); \
    assert(_xasprintf_result != -1);                   \
}
#endif

#endif /* _TARPM_HELPERS_H */
