/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <stdlib.h>
#include <assert.h>
#include <err.h>

/* Always 0-initialized, unless the compiler disagrees. */
void *
xcalloc(size_t n, size_t s)
{
    void *ret = NULL;

    ret = calloc(n, s);

    if (ret == NULL) {
        err(EXIT_FAILURE, "calloc");
    }

    return ret;
}

void *
xalloc(size_t s)
{
    return xcalloc(1, s);
}

/* Note that xrealloc(NULL, ...) works the way you'd want. */
void *
xrealloc(void *p, size_t s)
{
    void *ret;

    if (p == NULL) {
        return xalloc(s);
    }

    ret = realloc(p, s);

    if (ret == NULL) {
        err(EXIT_FAILURE, "realloc");
    }

    return ret;
}

#ifdef _HAVE_REALLOCARRAY
void *
xreallocarray(void *p, size_t n, size_t s)
{
    void *ret;

    if (p == NULL) {
        return xcalloc(n, s);
    }

    ret = reallocarray(p, n, s);

    if (ret == NULL) {
        err(EXIT_FAILURE, "reallocarray");
    }

    return ret;
}
#endif
