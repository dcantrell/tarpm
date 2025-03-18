/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TARPM_TYPES_H
#define _TARPM_TYPES_H

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
    short type;
    short archnum;
    char name[66];
    short osnum;
    short signature_type;  /*!< Signature header type (RPMSIG_HEADERSIG) */
    char reserved[16];     /*!< Pad to 96 bytes -- 8 byte aligned! */
};

/*
 * The next two types are in the "signature" and "header" header.
 */
struct rpmsignature {
    uint32_t magic;        /* must be "\216\255\350\001" */
    uint32_t reserved;     /* must be "\0\0\0\0" */
    uint32_t nentries;     /* number of index records */
    uint32_t nbytes;       /* size of storage area for data */
};

/*
 * Header and signature structure.
 */
struct rpmsigvalues {
    uint32_t ilen;
    uint32_t hlen;
    struct rpmidxentry *estart;
    struct rpmidxentry *entry;
    uint8_t *datastart;
    uint32_t padlen;
    struct rpmsignature pad;
};

/* the size of the header intro to read from the file */
#define RPMHDRINTROSZ (sizeof(uint32_t) * 4)

struct rpmidxentry {
    uint32_t tag;          /* the key */
    uint32_t type;         /* the data type */
    int32_t offset;        /* where to find the data in the storage area */
    uint32_t count;        /* how many data items are stored in this key */
};

/* A union for data types used when extracting data from the header. */
union datatypes
{
    char c;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
};

#endif /* _TARPM_TYPES_H */
