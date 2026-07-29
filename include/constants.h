/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_CONSTANTS_H
#define _TARPM_CONSTANTS_H

/* the name of the program and, ideally, the executable command */
#define COMMAND_NAME                 "tarpm"

/* the "architecture" name for source RPMs */
#define SRPM_ARCH_NAME               "src"

/* the subdirectory where the RPM payload is unpacked */
#define PAYLOAD_SUBDIR               "payload"

/* output filenames for headers */
#define OUTPUT_LEAD                  "lead.json"
#define OUTPUT_SIGNATURE             "signature.json"
#define OUTPUT_HEADER                "header.json"

/* output file endings (extensions) */
#define OUTPUT_TXT_ENDING            "txt"

/* RPM lead -- all from rpm headers and source */
#define RPMLEAD_MAGIC0               0xED
#define RPMLEAD_MAGIC1               0xAB
#define RPMLEAD_MAGIC2               0xEE
#define RPMLEAD_MAGIC3               0xDB

#define RPMSIGTYPE_HEADERSIG         5

#define RPMLEAD_SIZE                 96

/* RPM lead fields and descriptions */
#define RPM_LEAD_MAGIC               "lead magic"
#define RPM_LEAD_VERSION             "version"
#define RPM_LEAD_TYPE                "type"
#define RPM_LEAD_NAME                "name"
#define RPM_LEAD_ARCH                "architecture"
#define RPM_LEAD_OS                  "os"
#define RPM_LEAD_SIGTYPE             "signature type"

/* RPM signature/header fields and values */
#define RPM_SIGNATURE_MAGIC_DESC     "magic"
#define RPM_SIGNATURE_RESERVED_DESC  "reserved"
#define RPM_SIGNATURE_MAGIC          0x8EADE801
#define RPM_SIGNATURE_RESERVED       0
#define RPM_ENTRY_NAME_DESC          "name"
#define RPM_ENTRY_TAG_DESC           "number"
#define RPM_ENTRY_TYPE_DESC          "type"
#define RPM_ENTRY_TAGS_DESC          "tags"
#define RPM_ENTRY_VALUE_DESC         "value"
#define RPM_ENTRY_FILE_DESC          "file"
#define RPM_ENTRY_TRAILER_DESC       "trailer"

/* general purpose string constants */
#define RPM_METADATA_READ_ONLY       "read-only"

/* Digest types used in the headers */
#define TARPM_DIGEST_MD5             1
#define TARPM_DIGEST_SHA1            2
#define TARPM_DIGEST_SHA256          3

#endif /* _TARPM_CONSTANTS_H */
