/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * fwrite() wrapper for integration tests to simulate failure cases.
 * This is loaded with LD_PRELOAD so our test runtime uses this rather
 * than the standard fwrite().  Simulate a failure by setting
 * FAIL_FWRITE to N, which fails only the Nth call.  Every other call
 * goes to the standard fwrite(), so the writes after the failed one
 * still succeed.  The failed call sets errno to EIO and returns 0.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t
fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    static size_t (*real_fwrite)(const void *, size_t, size_t, FILE *) = NULL;
    static int calls = 0;
    const char *fail = getenv("FAIL_FWRITE");
    void *sym = NULL;

    if (real_fwrite == NULL) {
        sym = dlsym(RTLD_NEXT, "fwrite");
        memcpy(&real_fwrite, &sym, sizeof(real_fwrite));
    }

    calls++;

    if (fail != NULL && calls == atoi(fail)) {
        errno = EIO;
        return 0;
    }

    return real_fwrite(ptr, size, nmemb, stream);
}
