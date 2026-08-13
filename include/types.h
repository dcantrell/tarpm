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

/*
 * Header lists in the RPM metadata that contain per-entry values for
 * the RPM payload.  This is just a convenience grouping to hold all
 * of the metadata lists that will be used to construct libarchive
 * entries.
 */
struct hdr_file_lists {
    struct json_object *basenames;
    struct json_object *dirnames;
    struct json_object *dirindexes;
    struct json_object *filesizes;
    struct json_object *filemodes;
    struct json_object *fileuids;
    struct json_object *filegids;
    struct json_object *filerdevs;
    struct json_object *filemtimes;
    struct json_object *filelinktos;
    struct json_object *fileinodes;
};

/*
 * Payload entries need all of these values from the RPM header
 * metadata.  This struct is to help get that info over to libarchive.
 * The entries in this struct match entries from RPM header tags for
 * the file metadata.  RPM divides up the information in a unique way.
 * We update the data in our structure from the actual filesystem if
 * it differs from what's in the RPM header metadata.
 */
struct file_params {
    /* from RPMTAG_DIRNAMES */
    const char *dirname;

    /* from RPMTAG_BASENAMES */
    const char *basename;

    /* location of the file in our payload subdir */
    const char *payload_subdir;

    /* from RPMTAG_FILESIZES */
    uint64_t size;

    /* from RPMTAG_FILEMODES */
    uint16_t mode;

    /* from RPMTAG_FILEUIDS */
    uint32_t uid;

    /* from RPMTAG_FILEGIDS */
    uint32_t gid;

    /* from RPMTAG_FILERDEVS */
    uint16_t rdev;

    /* from RPMTAG_FILEMTIMES */
    uint32_t mtime;

    /* from RPMTAG_FILELINKTOS */
    const char *linkto;

    /* from RPMTAG_FILEINODES */
    uint32_t inode;

    /* from RPMTAG_FILENLINKS */
    uint32_t nlink;

    /* path to the first occurrence of the hardlink in our payload subdir */
    const char *hardlink;
};

/* Function pointers */
typedef void (*list_entry_data_free_func)(void *);

#endif /* _TARPM_TYPES_H */
