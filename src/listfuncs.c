/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tarpm.h"

/**
 * @brief Join all members of a str_list_t in to a single string.
 *
 * Given a str_list_t, combine all the members in to a newly
 * allocated string.  An optional delimiter can be provided by passing
 * a string as the delimiter argument.  If NULL given as the
 * delimited, all strings will be concatenated together.  Caller is
 * responsible for freeing memory allocated by this function.
 *
 * @param list str_list_t containing members to join
 * @param delimiter Optional delimiter string to put between each list
 *        member (NULL to disable)
 * @return Newly allocated string of concatenated list members; caller
 *         must free.
 */
char *list_to_string(const str_list_t *list, const char *delimiter)
{
    size_t pos = 0;
    char *s = NULL;
    str_entry_t *entry = NULL;

    if (list == NULL || TAILQ_EMPTY(list)) {
        return NULL;
    }

    s = strdup("");

    if (s == NULL) {
        err(EXIT_FAILURE, "strdup");
    }

    TAILQ_FOREACH(entry, list, items) {
        if (pos > 0 && delimiter != NULL) {
            s = strappend(s, delimiter, NULL);
        }

        s = strappend(s, entry->str, NULL);
        pos++;
    }

    return s;
}

/*
 * Helper function to free a str_list_t and each entry->str.  If
 * the free_func is NULL, nothing is done to the entry->str values.
 */
void list_free(str_list_t *list, list_entry_data_free_func free_func)
{
    str_entry_t *entry = NULL;

    if (list == NULL) {
        return;
    }

    while (!TAILQ_EMPTY(list)) {
        entry = TAILQ_FIRST(list);
        TAILQ_REMOVE(list, entry, items);

        if (free_func != NULL) {
            free_func(entry->str);
        }

        free(entry);
    }

    free(list);
    return;
}

/*
 * Append the string to the str_list_t and return the
 * str_list_t.  A NULL string is not added and the caller just gets
 * back a pointer to the same str_list_t.  A NULL list may be
 * specified, in which case the function will start a new list and add
 * the string to it.  Caller responsible for all memory management.
 */
str_list_t *list_add(str_list_t *list, const char *s)
{
    str_entry_t *entry = NULL;

    if (s == NULL) {
        return list;
    }

    if (list == NULL) {
        list = xalloc(sizeof(*list));
        TAILQ_INIT(list);
    }

    entry = xalloc(sizeof(*entry));
    entry->str = strdup(s);

    if (entry->str == NULL) {
        warn("strdup");
    }

    TAILQ_INSERT_TAIL(list, entry, items);

    return list;
}
