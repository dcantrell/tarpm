/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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

/* RPM lead */
#define RPMLEAD_SIZE       96

/* RPM lead fields and descriptions */
#define RPM_LEAD_MAGIC               "lead magic"
#define RPM_LEAD_VERSION             "version"
#define RPM_LEAD_TYPE                "type"
#define RPM_LEAD_NAME                "name"
#define RPM_LEAD_ARCH                "architecture"
#define RPM_LEAD_OS                  "os"
#define RPM_LEAD_SIGTYPE             "signature type"

/* RPM lead field values */
/* (from RPM's header files */
#define RPM_LEAD_SOURCE              "RPMLEAD_SOURCE"
#define RPM_LEAD_BINARY              "RPMLEAD_BINARY"
#define RPM_LEAD_HEADERSIG           "RPMSIGTYPE_HEADERSIG"
#define RPM_LEAD_UNKNOWN             "unknown"

/* RPM signature fields and values */
#define RPM_SIGNATURE_MAGIC_DESC     "magic"
#define RPM_SIGNATURE_RESERVED_DESC  "reserved"
#define RPM_SIGNATURE_NENTRIES_DESC  "index entries"
#define RPM_SIGNATURE_ILEN_DESC      "index size (bytes)"
#define RPM_SIGNATURE_NBYTES_DESC    "data size (bytes)"
#define RPM_SIGNATURE_HLEN_DESC      "header size (bytes)"
#define RPM_SIGNATURE_MAGIC          0x8EADE801
#define RPM_SIGNATRURE_RESERVED      0
#define RPM_SIGNATURE_NAME_DESC      "name"
#define RPM_SIGNATURE_TAG_DESC       "number"
#define RPM_SIGNATURE_TYPE_DESC      "type"
#define RPM_SIGNATURE_OFFSET_DESC    "offset"
#define RPM_SIGNATURE_COUNT_DESC     "count"
#define RPM_SIGNATURE_TAGS_DESC      "tags"
#define RPM_SIGNATURE_VALUE_DESC     "value"
#define RPM_SIGNATURE_VALUES_DESC    "values"

#endif /* _TARPM_CONSTANTS_H */
