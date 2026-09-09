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
char *
list_to_string(const str_list_t *list, const char *delimiter)
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
 * Append the string to the str_list_t and return the
 * str_list_t.  A NULL string is not added and the caller just gets
 * back a pointer to the same str_list_t.  A NULL list may be
 * specified, in which case the function will start a new list and add
 * the string to it.  Caller responsible for all memory management.
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
    entry->str = strdup(s);

    if (entry->str == NULL) {
        warn("strdup");
    }

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
 * Append the value to the uint32_list_t and return the uint32_list_t.
 * A NULL list may be specified, in which case the function will start
 * a new list and add the value to it.  Caller responsible for all
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
 * Return the string at the given index in a str_list_t or NULL if the
 * list is NULL or the index is out of range.
 */
const char *
str_list_nth(const str_list_t *list, uint32_t index)
{
    uint32_t i = 0;
    str_entry_t *entry = NULL;

    if (list == NULL) {
        return NULL;
    }

    TAILQ_FOREACH(entry, list, items) {
        if (i == index) {
            return entry->str;
        }

        i++;
    }

    return NULL;
}

/*
 * Return the number at the given index in a uint32_list_t through the
 * value argument.  Returns false if the list is NULL or the index is
 * out of range, in which case value is ignored.
 */
bool
uint32_list_nth(const uint32_list_t *list, uint32_t index, uint32_t *value)
{
    uint32_t i = 0;
    uint32_entry_t *entry = NULL;

    if (list == NULL || value == NULL) {
        return false;
    }

    TAILQ_FOREACH(entry, list, items) {
        if (i == index) {
            *value = entry->value;
            return true;
        }

        i++;
    }

    return false;
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
