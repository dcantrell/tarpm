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
    short signature_type;       /*!< Signature header type (RPMSIG_HEADERSIG) */
    char reserved[16];          /*!< Pad to 96 bytes -- 8 byte aligned! */
};

/*
 * The next two sections are in the "signature" header.
 */
struct rpmsighdr {
    unsigned char magic[4];     /* must be "\216\255\350\001" */
    unsigned char reserved[4];  /* must be "\0\0\0\0" */
    int nindex;                 /* number of index records */
    int hsize;                  /* size of storage area for data */
};

struct rpmsighdridx {
    int tag;                    /* the key */
    int type;                   /* the data type */
    int offset;                 /* where to find the data in the storage area */
    int count;                  /* how many data items are stored in this key */
};

#endif /* _TARPM_TYPES_H */
