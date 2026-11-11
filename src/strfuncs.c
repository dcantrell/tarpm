/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>

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
            dest = xstrdup(s);
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

    walk = xstrdup(s);
    walkp = walk;

    /* split the string and build the list */
    while ((token = strsep(&walk, delim)) != NULL) {
        list = list_add(list, token);
    }

    free(walkp);
    return list;
}

/*
 * Return a copy of s with control characters turned into \ooo escapes
 * and backslashes doubled, the way ls -b prints names.  Bytes of 0x80
 * and up are kept so UTF-8 names stay readable.  Caller must free the
 * result.
 */
char *
escape_name(const char *s)
{
    char *result = NULL;
    char *out = NULL;
    const unsigned char *c = NULL;

    if (s == NULL) {
        return NULL;
    }

    /* every byte grows to at most four */
    result = xalloc((strlen(s) * 4) + 1);
    out = result;

    for (c = (const unsigned char *) s; *c != '\0'; c++) {
        if (*c == '\\') {
            *out++ = '\\';
            *out++ = '\\';
        } else if (*c < 0x20 || *c == 0x7f) {
            out += sprintf(out, "\\%03o", *c);
        } else {
            *out++ = (char) *c;
        }
    }

    *out = '\0';
    return result;
}

/* Format a message and pass it to warnx() with escape_name() applied. */
void
warnx_escaped(const char *fmt, ...)
{
    va_list ap;
    char *msg = NULL;
    char *escaped = NULL;

    va_start(ap, fmt);

    if (vasprintf(&msg, fmt, ap) == -1) {
        va_end(ap);
        warnx("%s", fmt);
        return;
    }

    va_end(ap);
    escaped = escape_name(msg);
    warnx("%s", escaped);
    free(escaped);
    free(msg);
    return;
}

/* The warn() version of warnx_escaped(), errno is kept for warn(). */
void
warn_escaped(const char *fmt, ...)
{
    int saved = errno;
    va_list ap;
    char *msg = NULL;
    char *escaped = NULL;

    va_start(ap, fmt);

    if (vasprintf(&msg, fmt, ap) == -1) {
        va_end(ap);
        errno = saved;
        warn("%s", fmt);
        return;
    }

    va_end(ap);
    escaped = escape_name(msg);
    errno = saved;
    warn("%s", escaped);
    free(escaped);
    free(msg);
    return;
}

/* Give back s, or "" when s is NULL, for callers that compare strings. */
const char *
str_or_empty(const char *s)
{
    if (s == NULL) {
        return "";
    }

    return s;
}

/*
 * True with -v.  The status lines go to verbose_out, which is stdout
 * unless the caller points it elsewhere.  Extracting with a JSON file
 * going to stdout sends them to stderr, the way tar(1) does.
 */
bool verbose = false;
FILE *verbose_out = NULL;

/* Print one escaped status line with the given indent */
static void
verbose_line(const char *indent, const char *fmt, va_list ap)
{
    char *msg = NULL;
    char *escaped = NULL;
    FILE *out = verbose_out;

    if (out == NULL) {
        out = stdout;
    }

    if (vasprintf(&msg, fmt, ap) == -1) {
        return;
    }

    escaped = escape_name(msg);
    fprintf(out, "%s%s\n", indent, escaped);
    free(escaped);
    free(msg);
    return;
}

/*
 * Print the line for a stage of the work with -v.  Stages start with
 * VERBOSE_STAGE_PREFIX and the details of each one sit under it.
 */
void
verbose_stage(const char *fmt, ...)
{
    va_list ap;

    if (!verbose) {
        return;
    }

    va_start(ap, fmt);
    verbose_line(VERBOSE_STAGE_PREFIX, fmt, ap);
    va_end(ap);
    return;
}

/* Print a detail line under the current stage with -v */
void
verbose_detail(const char *fmt, ...)
{
    va_list ap;

    if (!verbose) {
        return;
    }

    va_start(ap, fmt);
    verbose_line(VERBOSE_DETAIL_INDENT, fmt, ap);
    va_end(ap);
    return;
}

/*
 * Write a byte count to buf for people to read.  Under 1 KiB it is a
 * count of bytes, above that one decimal place of KiB, MiB or GiB.
 */
void
human_size(const uint64_t bytes, char *buf, const size_t len)
{
    size_t i = 0;
    double value = (double) bytes;
    const char *units[] = { "KiB", "MiB", "GiB", "TiB" };

    if (buf == NULL || len == 0) {
        return;
    }

    if (bytes < 1024) {
        if (bytes == 1) {
            snprintf(buf, len, _("1 byte"));
        } else {
            snprintf(buf, len, _("%" PRIu64 " bytes"), bytes);
        }

        return;
    }

    value /= 1024.0;

    while (value >= 1024.0 && i < (sizeof(units) / sizeof(units[0])) - 1) {
        value /= 1024.0;
        i++;
    }

    snprintf(buf, len, "%.1f %s", value, units[i]);
    return;
}
