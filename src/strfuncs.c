/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include "tarpm.h"

/*
 * Returns true if s starts with prefix.
 */
bool
strprefix(const char *s, const char *prefix)
{
    size_t plen = 0;

    if (s == NULL || prefix == NULL) {
        return false;
    }

    plen = strlen(prefix);

    if (plen > strlen(s)) {
        return false;
    }

    if (!strncmp(s, prefix, plen)) {
        return true;
    } else {
        return false;
    }
}

/*
 * Test if the string STR ends up with the string SUFFIX.
 * Both strings have to be NULL terminated.
 */
bool
strsuffix(const char *s, const char *suffix)
{
    size_t sl = 0;
    size_t xl = 0;

    if (s == NULL || suffix == NULL) {
        return false;
    }

    sl = strlen(s);
    xl = strlen(suffix);

    if ((sl >= xl) && !strcmp(s + sl - xl, suffix)) {
        return true;
    } else {
        return false;
    }
}

/*
 * Append one or more strings to dest and return the result.  All
 * items to append must be of type 'char *'.  Terminate the list with
 * NULL.  Memory is allocated or reallocated and the first argument is
 * modified.  Caller is responsible for freeing memory.
 */
char *
strappend(char *dest, ...)
{
    va_list sl;
    const char *s = NULL;

    va_start(sl, dest);

    while ((s = va_arg(sl, const char *)) != NULL) {
        if (dest == NULL) {
            dest = strdup(s);
            assert(dest != NULL);
        } else {
            dest = xrealloc(dest, strlen(dest) + strlen(s) + 1);
            dest = strcat(dest, s);
        }
    }

    va_end(sl);

    return dest;
}

/*
 * Split given string on delimiter.  Put each substring in a
 * str_list_t as a separate entry, return the list.  Caller must free
 * the list.
 */
str_list_t *strsplit(const char *s, const char *delim)
{
    char *walk = NULL;
    char *walkp = NULL;
    char *token = NULL;
    str_list_t *list = NULL;

    if (s == NULL) {
        return NULL;
    }

    /* given a string but no delim, just make a single entry list */
    if (s && (delim == NULL || !strcmp(s, delim))) {
        list = list_add(list, s);
        return list;
    }

    walk = strdup(s);
    assert(walk != NULL);
    walkp = walk;

    /* split the string and build the list */
    while ((token = strsep(&walk, delim)) != NULL) {
        list = list_add(list, token);
    }

    free(walkp);
    return list;
}
