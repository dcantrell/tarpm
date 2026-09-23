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

/* A path ending and the class rpm gives every file that has it. */
struct class_extension {
    const char *extension;
    const char *name;
};

/*
 * Path endings rpm answers itself instead of asking libmagic.  This
 * saves the call and keeps the answer the same everywhere.  This is
 * the skipped_extensions table in build/rpmfc.cc in the rpm source.
 */
static const struct class_extension class_extensions[] = {
    { ".pm",   "Perl5 module source text" },
    { ".c",    "C Code" },
    { ".h",    "C Header" },
    { ".la",   "libtool library file" },
    { ".pc",   "pkgconfig file" },
    { ".html", "HTML document" },
    { ".png",  "PNG image data" },
    { ".svg",  "SVG Scalable Vector Graphics image" },
    { NULL,    NULL }
};

/* The libmagic handle, opened the first time we need it. */
static magic_t magic_handle = NULL;

/* Set once we have tried to open the handle, so we only try once. */
static bool magic_opened = false;

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
 * Give back the class rpm writes for a path ending, or NULL when the
 * path does not end with one we answer ourselves.
 */
const char *
skipped_class(const char *path)
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
            return class_extensions[i].name;
        }
    }

    return NULL;
}

/*
 * Give back the libmagic handle we classify with, opening it the first
 * time we are asked.  We ask libmagic for the same things rpm does.
 * Returns NULL when libmagic will not start up, and then the caller
 * goes on without it.
 */
static magic_t
get_magic(void)
{
    int flags = MAGIC_CHECK | MAGIC_COMPRESS | MAGIC_NO_CHECK_TOKENS | MAGIC_ERROR;

    if (magic_opened) {
        return magic_handle;
    }

    magic_opened = true;
    magic_handle = magic_open(flags);

    if (magic_handle == NULL) {
        warn("magic_open");
        return NULL;
    }

    if (magic_load(magic_handle, NULL) == -1) {
        warnx(_("*** magic_load failed: %s"), magic_error(magic_handle));
        magic_close(magic_handle);
        magic_handle = NULL;
    }

    return magic_handle;
}

/* Close the libmagic handle if we opened one. */
void
close_magic(void)
{
    if (magic_handle != NULL) {
        magic_close(magic_handle);
        magic_handle = NULL;
    }

    magic_opened = false;

    return;
}

/*
 * Work out the class string rpm would record for a file.  The path is
 * where the file lands when the package is installed and file_path is
 * where it sits in the payload tree now.  Anything that is not a
 * regular file or a symlink gets a fixed name, some path endings get
 * one rpm writes itself, files under /dev/ get none, and the rest go
 * to libmagic.  A class rpm would not keep comes back as an empty
 * string.  The caller has to free what comes back.
 */
char *
file_class(const char *path, const char *file_path, const struct stat *sb)
{
    magic_t ms = NULL;
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
            name = skipped_class(path);

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

            ms = get_magic();

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
