/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_HELPERS_H
#define _TARPM_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <archive.h>

/* Macros */
#define xasprintf(dest, ...) {                         \
    *(dest) = NULL;                                    \
    if (asprintf((dest), __VA_ARGS__) == -1) {         \
        err(EXIT_FAILURE, "asprintf");                 \
    }                                                  \
}

/* libarchive compatibility */
#if ARCHIVE_VERSION_NUMBER < 3000000
#define archive_write_add_filter_bzip2 archive_write_set_compression_bzip2
#define archive_write_add_filter_compress archive_write_set_compression_compress
#define archive_write_add_filter_gzip archive_write_set_compression_gzip
#define archive_write_add_filter_zstd archive_write_set_compression_zstd
#define archive_write_add_filter_lzma archive_write_set_compression_lzma
#define archive_write_add_filter_xz archive_write_set_compression_xz
#define archive_write_add_filter_none archive_write_set_compression_none
#endif

/* json-c compatibility */
#if JSON_C_VERSION_NUM < 3584
/* the uint functions appeared in release 0.14.0 */
#define json_object_get_uint64 json_object_get_int64
#endif

#endif /* _TARPM_HELPERS_H */
