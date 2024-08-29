/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TARPM_CONSTANTS_H
#define _TARPM_CONSTANTS_H

/* the name of the program and, ideally, the executable command */
#define COMMAND_NAME       "tarpm"

/* the "architecture" name for source RPMs */
#define SRPM_ARCH_NAME     "src"

/* the subdirectory where the RPM payload is unpacked */
#define PAYLOAD_SUBDIR     "payload"

/* output filenames for headers */
#define OUTPUT_LEAD        "lead.json"
#define OUTPUT_SIGNATURE   "signature.json"
#define OUTPUT_HEADER      "header.json"

/* RPM lead */
#define RPMLEAD_SIZE       96

/* RPM lead fields and descriptions */
#define RPM_LEAD_MAGIC     "Lead Magic"
#define RPM_LEAD_VERSION   "Version"
#define RPM_LEAD_TYPE      "Type"
#define RPM_LEAD_NAME      "Name"
#define RPM_LEAD_ARCH      "Architecture"
#define RPM_LEAD_OS        "OS"
#define RPM_LEAD_SIGTYPE   "Signature Type"

/* RPM lead field values */
/* (from RPM's header files */
#define RPM_LEAD_SOURCE    "RPMLEAD_SOURCE"
#define RPM_LEAD_BINARY    "RPMLEAD_BINARY"
#define RPM_LEAD_HEADERSIG "RPMSIGTYPE_HEADERSIG"
#define RPM_LEAD_UNKNOWN   "unknown"

#endif /* _TARPM_CONSTANTS_H */
