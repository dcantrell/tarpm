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

#endif /* _TARPM_TYPES_H */
