/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <magic.h>
#include <sys/stat.h>

#include "tarpm.h"

/*
 * The strings rpm looks for in what libmagic says about a file.  A
 * file whose type holds one of these keeps that type as its class and
 * every other file gets no class at all.  This is the rpmfcTokens
 * table in build/rpmfc.cc in the rpm source.
 */
static const char *class_tokens[] = {
    "directory",

    "ELF 32-bit",
    "ELF 64-bit",

    "troff or preprocessor input",
    "GNU Info",

    "perl ",
    "Perl5 module source text",
    "python ",

    "libtool library ",
    "pkgconfig ",

    "Objective caml ",
    "Mono/.Net assembly",

    "current ar archive",
    "Zip archive data",
    "tar archive",
    "cpio archive",
    "RPM v3",
    "RPM v4",

    " image",
    " font",
    " Font",

    " commands",
    " script",

    "empty",

    "HTML",
    "SGML",
    "XML",

    " source",
    "GLS_BINARY_LSB_FIRST",
    " DB ",

    " text",

    NULL
};

/*
 * Path endings rpm answers itself instead of asking libmagic.  This
 * saves the call and keeps the answer the same everywhere.  This is
 * the skipped_extensions table in build/rpmfc.cc in the rpm source.
 */
static const struct class_extension class_extensions[] = {
    { ".pm",   "Perl5 module source text",           "text/plain" },
    { ".c",    "C Code",                             "text/x-c" },
    { ".h",    "C Header",                           "text/x-c" },
    { ".la",   "libtool library file",               "text/plain" },
    { ".pc",   "pkgconfig file",                     "text/plain" },
    { ".html", "HTML document",                      "text/html" },
    { ".png",  "PNG image data",                     "image/png" },
    { ".svg",  "SVG Scalable Vector Graphics image", "image/svg+xml" },
    { NULL,    NULL,                                 NULL }
};

/* The libmagic handles, opened the first time we need each one. */
static magic_t magic_handle = NULL;
static magic_t mime_handle = NULL;

/* Set once we have tried to open a handle, so we only try once. */
static bool magic_opened = false;
static bool mime_opened = false;

/*
 * Tell whether rpm keeps the type string libmagic handed back.  rpm
 * only records the ones that hold a token it knows and leaves the
 * rest with no class.
 */
bool
wanted_class(const char *name)
{
    size_t i = 0;

    if (name == NULL) {
        return false;
    }

    for (i = 0; class_tokens[i] != NULL; i++) {
        if (strstr(name, class_tokens[i]) != NULL) {
            return true;
        }
    }

    return false;
}

/*
 * Find the entry in the table of path endings rpm answers itself, or
 * NULL when the path does not end with one of them.
 */
const struct class_extension *
skipped_extension(const char *path)
{
    size_t i = 0;
    size_t len = 0;
    size_t extlen = 0;

    if (path == NULL) {
        return NULL;
    }

    len = strlen(path);

    for (i = 0; class_extensions[i].extension != NULL; i++) {
        extlen = strlen(class_extensions[i].extension);

        if (len >= extlen && !strcmp(path + len - extlen, class_extensions[i].extension)) {
            return &class_extensions[i];
        }
    }

    return NULL;
}

/*
 * Only open a handle for libmagic on the first call.  Return NULL if
 * libmagic fails.
 */
static magic_t
open_magic(magic_t *handle, bool *opened, const int flags)
{
    if (*opened) {
        return *handle;
    }

    *opened = true;
    *handle = magic_open(flags);

    if (*handle == NULL) {
        warn("magic_open");
        return NULL;
    }

    if (magic_load(*handle, NULL) == -1) {
        warnx(_("*** magic_load failed: %s"), magic_error(*handle));
        magic_close(*handle);
        *handle = NULL;
    }

    return *handle;
}

/* Close the libmagic handles if we opened any. */
void
close_magic(void)
{
    if (magic_handle != NULL) {
        magic_close(magic_handle);
        magic_handle = NULL;
    }

    if (mime_handle != NULL) {
        magic_close(mime_handle);
        mime_handle = NULL;
    }

    magic_opened = false;
    mime_opened = false;

    return;
}

/*
 * Determine the class string for a file in the payload.  The path is
 * relative to the payload root tree, file_path is the full path to the
 * file in the payload tree.
 *
 * Caller must free the returned string.
 */
char *
file_class(const char *path, const char *file_path, const struct stat *sb)
{
    magic_t ms = NULL;
    const struct class_extension *ext = NULL;
    const char *name = NULL;

    if (path == NULL || file_path == NULL || sb == NULL) {
        return NULL;
    }

    switch (sb->st_mode & S_IFMT) {
        case S_IFCHR:
            name = RPM_FILE_CLASS_CHARDEV;
            break;
        case S_IFBLK:
            name = RPM_FILE_CLASS_BLOCKDEV;
            break;
        case S_IFIFO:
            name = RPM_FILE_CLASS_PIPE;
            break;
        case S_IFSOCK:
            name = RPM_FILE_CLASS_SOCKET;
            break;
        case S_IFDIR:
            name = RPM_FILE_CLASS_DIR;
            break;
        default:
            ext = skipped_extension(path);
            name = (ext == NULL) ? NULL : ext->name;

            /*
             * Files under /dev/ stand in for the real device nodes the
             * system makes, so rpm leaves them with no class even when
             * the path ending says otherwise.
             */
            if (strlen(path) >= sizeof(RPM_FILE_CLASS_DEV_PREFIX) && !strncmp(path, RPM_FILE_CLASS_DEV_PREFIX, sizeof(RPM_FILE_CLASS_DEV_PREFIX) - 1)) {
                name = RPM_FILE_CLASS_NONE;
                break;
            }

            if (name != NULL) {
                break;
            }

            ms = open_magic(&magic_handle, &magic_opened, RPM_MAGIC_FLAGS);

            if (ms != NULL) {
                errno = 0;
                name = magic_file(ms, file_path);

                /* nothing there to look at, so there is nothing to say */
                if (name == NULL && errno == ENOENT) {
                    name = RPM_FILE_CLASS_NONE;
                }
            }

            /* libmagic could not tell us, so rpm calls the file data */
            if (name == NULL) {
                if (ms != NULL) {
                    warnx(_("*** unable to recognize %s: %s"), path, magic_error(ms));
                }

                name = RPM_FILE_CLASS_DATA;
            }

            break;
    }

    if (!wanted_class(name)) {
        return strdup(RPM_FILE_CLASS_NONE);
    }

    return strdup(name);
}

/*
 * Determine the MIME type rpm would for a file in a v6 format
 * package.  rpmfcClassify() in build/rpmfc.cc in the rpm source asks
 * libmagic about every file whatever its type, except that a regular
 * file or symlink with a path ending it knows about gets the type
 * from its table.  A file that is not there has no MIME type and one
 * libmagic cannot read is application/octet-stream.
 *
 * Caller must free the returned string.
 */
char *
file_mime(const char *path, const char *file_path, const struct stat *sb)
{
    magic_t ms = NULL;
    const struct class_extension *ext = NULL;
    const char *name = NULL;

    if (path == NULL || file_path == NULL || sb == NULL) {
        return NULL;
    }

    switch (sb->st_mode & S_IFMT) {
        case S_IFCHR:
        case S_IFBLK:
        case S_IFIFO:
        case S_IFSOCK:
        case S_IFDIR:
            break;
        default:
            ext = skipped_extension(path);
            name = (ext == NULL) ? NULL : ext->mime;
            break;
    }

    if (name != NULL) {
        return strdup(name);
    }

    ms = open_magic(&mime_handle, &mime_opened, RPM_MAGIC_FLAGS | MAGIC_MIME_TYPE);

    if (ms != NULL) {
        errno = 0;
        name = magic_file(ms, file_path);

        /* nothing there to look at, so there is nothing to say */
        if (name == NULL && errno == ENOENT) {
            name = RPM_FILE_MIME_NONE;
        }
    }

    if (name == NULL) {
        if (ms != NULL) {
            warnx(_("*** unable to recognize %s: %s"), path, magic_error(ms));
        }

        name = RPM_FILE_MIME_UNKNOWN;
    }

    return strdup(name);
}
