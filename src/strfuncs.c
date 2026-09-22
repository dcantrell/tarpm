/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <stdarg.h>
#include <string.h>

#include "tarpm.h"

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

            if (dest == NULL) {
                err(EXIT_FAILURE, "strdup");
            }
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
    if (delim == NULL || !strcmp(s, delim)) {
        list = list_add(list, s);
        return list;
    }

    walk = strdup(s);
    walkp = walk;

    if (walk == NULL) {
        err(EXIT_FAILURE, "strdup");
    }

    /* split the string and build the list */
    while ((token = strsep(&walk, delim)) != NULL) {
        list = list_add(list, token);
    }

    free(walkp);
    return list;
}
