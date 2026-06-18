/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_TYPES_H
#define _TARPM_TYPES_H

#include "queue.h"

/*
 * This is from lib/rpmlead.c in rpm's source.  The RPM "lead" is
 * legacy, but is part of every RPM.  The type and handling of this is
 * not part of the public librpm API, but we need it here in order to
 * reconstruct an RPM.
 */
struct rpmlead {
    unsigned char magic[4];
    unsigned char major;
    unsigned char minor;
    int16_t type;
    int16_t archnum;
    char name[66];
    int16_t osnum;
    int16_t signature_type;  /*!< Signature header type (RPMSIG_HEADERSIG) */
    char reserved[16];       /*!< Pad to 96 bytes -- 8 byte aligned! */
};

/*
 * The next two types are in the "signature" and "header" header.
 * This is the core structure that begins a new header.  The way the
 * format is defined is that an RPM can have multiple headers like
 * this.  Right now RPM files have two: one called a "signature" and
 * one called a "header".  The latter contains all of the metadata
 * most packagers are looking for.
 */
struct rpmhdr {
    uint32_t magic;        /* must be "\216\255\350\001" */
    uint32_t reserved;     /* must be "\0\0\0\0" */
    uint32_t nentries;     /* number of index records */
    uint32_t nbytes;       /* size of storage area for data */
};

/*
 * Computed values for an rpmhdr.
 */
struct rpmhdrinfo {
    uint32_t ilen;
    uint32_t hlen;
    struct rpmhdrentry *estart;
    struct rpmhdrentry *entry;
    uint8_t *datastart;
    uint32_t padlen;
    struct rpmhdr pad;
};

/* the size of the header intro to read from the file */
#define RPMHDRINTROSZ (sizeof(uint32_t) * 4)

struct rpmhdrentry {
    uint32_t tag;          /* the key */
    uint32_t type;         /* the data type */
    int32_t offset;        /* where to find the data in the storage area */
    uint32_t count;        /* how many data items are stored in this key */
};

/* A union for data types used when extracting data from the header. */
union datatypes
{
    char c;
    uint8_t i8;
    uint16_t i16;
    uint32_t i32;
    uint64_t i64;
};

/*
 * List of strings.
 */
typedef struct _str_entry_t {
    char *str;
    TAILQ_ENTRY(_str_entry_t) items;
} str_entry_t;

typedef TAILQ_HEAD(str_entry_s, _str_entry_t) str_list_t;

/* Function pointers */
typedef void (*list_entry_data_free_func)(void *);

#endif /* _TARPM_TYPES_H */
