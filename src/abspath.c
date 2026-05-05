/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "tarpm.h"

/*
 * Canonicalize a path with relative references.  Does not rely on
 * filesystem existence.  Caller must free the returned string.
 */
char *abspath(const char *path)
{
    char *r = NULL;
    char *p = NULL;
    const char *delim = "/";
    str_list_t *tokens = NULL;
    str_list_t *newpath = NULL;
    str_entry_t *token = NULL;
    str_entry_t *element = NULL;

    if (path == NULL) {
        return NULL;
    }

    if (!strcmp(path, "") || !strcmp(path, delim)) {
        return strdup(path);
    }

    /* split path in to tokens */
    tokens = strsplit(path, delim);
    assert(tokens != NULL);

    /* our new path elements */
    newpath = xalloc(sizeof(*newpath));
    TAILQ_INIT(newpath);

    /* handle each part of the path */
    TAILQ_FOREACH(token, tokens, items) {
        if (!strcmp(token->str, "") || !strcmp(token->str, ".") || (!strcmp(token->str, "..") && TAILQ_EMPTY(newpath))) {
            /* no need to add this token */
            continue;
        } else if (!strcmp(token->str, "..") && !TAILQ_EMPTY(newpath)) {
            /* back up a path element */
            element = TAILQ_LAST(newpath, str_entry_s);
            TAILQ_REMOVE(newpath, element, items);
            free(element->str);
            free(element);
        } else {
            /* take this path element */
            newpath = list_add(newpath, token->str);
        }
    }

    /* generate the final path string */
    p = list_to_string(newpath, "/");

    if (p) {
        xasprintf(&r, "/%s", p);
    } else {
        r = strdup("/");
    }

    /* clean up */
    list_free(tokens, free);
    list_free(newpath, free);
    free(p);

    return r;
}
