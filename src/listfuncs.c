/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tarpm.h"

/**
 * @brief Join all members of a str_list_t in to a single string.
 *
 * Combine all the members of a str_list_t in to a newly allocated
 * string.  The delimiter goes between each member, or pass NULL to
 * run them together.  Caller must free the string.
 *
 * @param list str_list_t containing members to join
 * @param delimiter Optional delimiter string to put between each list
 *        member (NULL to disable)
 * @return Newly allocated string of concatenated list members.  Caller
 *         must free.
 */
char *
list_to_string(const str_list_t *list, const char *delimiter)
{
    size_t pos = 0;
    size_t len = 0;
    size_t dlen = 0;
    char *s = NULL;
    char *p = NULL;
    str_entry_t *entry = NULL;

    if (list == NULL || TAILQ_EMPTY(list)) {
        return NULL;
    }

    if (delimiter != NULL) {
        dlen = strlen(delimiter);
    }

    /* size the result first so we allocate once */
    TAILQ_FOREACH(entry, list, items) {
        if (pos > 0) {
            len += dlen;
        }

        len += strlen(entry->str);
        pos++;
    }

    s = xalloc(len + 1);
    p = s;
    pos = 0;

    TAILQ_FOREACH(entry, list, items) {
        if (pos > 0 && dlen > 0) {
            memcpy(p, delimiter, dlen);
            p += dlen;
        }

        len = strlen(entry->str);
        memcpy(p, entry->str, len);
        p += len;
        pos++;
    }

    *p = '\0';
    return s;
}

/*
 * Helper function to free a str_list_t and each entry->str.  If
 * the free_func is NULL, nothing is done to the entry->str values.
 */
void
list_free(str_list_t *list, list_entry_data_free_func free_func)
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
 * Append the string to the str_list_t and return the list.  A NULL
 * string is not added and the caller gets the same list back.  A NULL
 * list starts a new list with the string in it.  Caller handles all
 * memory management.
 */
str_list_t *
list_add(str_list_t *list, const char *s)
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
    entry->str = xstrdup(s);

    TAILQ_INSERT_TAIL(list, entry, items);

    return list;
}

/*
 * Helper function to free a uint32_list_t and each entry.  Safe to
 * call with a NULL list.
 */
void
uint32_list_free(uint32_list_t *list)
{
    uint32_entry_t *entry = NULL;

    if (list == NULL) {
        return;
    }

    while (!TAILQ_EMPTY(list)) {
        entry = TAILQ_FIRST(list);
        TAILQ_REMOVE(list, entry, items);
        free(entry);
    }

    free(list);
    return;
}

/*
 * Append the value to the uint32_list_t and return the list.  A NULL
 * list starts a new list with the value in it.  Caller handles all
 * memory management.
 */
uint32_list_t *
uint32_list_add(uint32_list_t *list, const uint32_t value)
{
    uint32_entry_t *entry = NULL;

    if (list == NULL) {
        list = xalloc(sizeof(*list));
        TAILQ_INIT(list);
    }

    entry = xalloc(sizeof(*entry));
    entry->value = value;
    TAILQ_INSERT_TAIL(list, entry, items);

    return list;
}

/*
 * Helper function to free a uint64_list_t and each entry.  Safe to
 * call with a NULL list.
 */
void
uint64_list_free(uint64_list_t *list)
{
    uint64_entry_t *entry = NULL;

    if (list == NULL) {
        return;
    }

    while (!TAILQ_EMPTY(list)) {
        entry = TAILQ_FIRST(list);
        TAILQ_REMOVE(list, entry, items);
        free(entry);
    }

    free(list);
    return;
}

/*
 * Append the value to the uint64_list_t and return the list.  A NULL
 * list starts a new list with the value in it.  Caller handles all
 * memory management.
 */
uint64_list_t *
uint64_list_add(uint64_list_t *list, const uint64_t value)
{
    uint64_entry_t *entry = NULL;

    if (list == NULL) {
        list = xalloc(sizeof(*list));
        TAILQ_INIT(list);
    }

    entry = xalloc(sizeof(*entry));
    entry->value = value;
    TAILQ_INSERT_TAIL(list, entry, items);

    return list;
}

/*
 * Return the number of entries in a str_list_t.  A NULL list has no
 * entries.
 */
uint32_t
str_list_len(const str_list_t *list)
{
    uint32_t count = 0;
    str_entry_t *entry = NULL;

    if (list == NULL) {
        return 0;
    }

    TAILQ_FOREACH(entry, list, items) {
        count++;
    }

    return count;
}

/*
 * Return the number of entries in a uint32_list_t.  A NULL list has no
 * entries.
 */
uint32_t
uint32_list_len(const uint32_list_t *list)
{
    uint32_t count = 0;
    uint32_entry_t *entry = NULL;

    if (list == NULL) {
        return 0;
    }

    TAILQ_FOREACH(entry, list, items) {
        count++;
    }

    return count;
}

/*
 * Helpers for walking one or more lists in lockstep.  Each one is
 * NULL-safe so callers can treat a missing list as a missing value.
 */
str_entry_t *
first_str(str_list_t *list)
{
    if (list == NULL) {
        return NULL;
    } else {
        return TAILQ_FIRST(list);
    }
}

uint32_entry_t *
first_uint32(uint32_list_t *list)
{
    if (list == NULL) {
        return NULL;
    } else {
        return TAILQ_FIRST(list);
    }
}

str_entry_t *
next_str(str_entry_t *entry)
{
    if (entry == NULL) {
        return NULL;
    } else {
        return TAILQ_NEXT(entry, items);
    }
}

uint32_entry_t *
next_uint32(uint32_entry_t *entry)
{
    if (entry == NULL) {
        return NULL;
    } else {
        return TAILQ_NEXT(entry, items);
    }
}

uint64_entry_t *
first_uint64(uint64_list_t *list)
{
    if (list == NULL) {
        return NULL;
    } else {
        return TAILQ_FIRST(list);
    }
}

uint64_entry_t *
next_uint64(uint64_entry_t *entry)
{
    if (entry == NULL) {
        return NULL;
    } else {
        return TAILQ_NEXT(entry, items);
    }
}
