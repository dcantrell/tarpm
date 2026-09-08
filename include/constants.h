/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_CONSTANTS_H
#define _TARPM_CONSTANTS_H

/* the name of the program and, ideally, the executable command */
#define COMMAND_NAME                   "tarpm"

/* the "architecture" name for source RPMs */
#define SRPM_ARCH_NAME                 "src"

/* the subdirectory where the RPM payload is unpacked */
#define PAYLOAD_SUBDIR                 "payload"

/* output filenames for headers */
#define OUTPUT_LEAD                    "lead.json"
#define OUTPUT_SIGNATURE               "signature.json"
#define OUTPUT_HEADER                  "header.json"

/* output file endings (extensions) */
#define OUTPUT_TXT_ENDING              "txt"

/* RPM lead -- all from rpm headers and source */
#define RPMLEAD_MAGIC0                 0xED
#define RPMLEAD_MAGIC1                 0xAB
#define RPMLEAD_MAGIC2                 0xEE
#define RPMLEAD_MAGIC3                 0xDB

#define RPMSIGTYPE_HEADERSIG           5

#define RPMLEAD_SIZE                   96

/* RPM lead fields and descriptions */
#define RPM_LEAD_MAGIC                 "lead magic"
#define RPM_LEAD_VERSION               "version"
#define RPM_LEAD_TYPE                  "type"
#define RPM_LEAD_NAME                  "name"
#define RPM_LEAD_ARCH                  "architecture"
#define RPM_LEAD_OS                    "os"
#define RPM_LEAD_SIGTYPE               "signature type"

/* RPM signature/header fields and values */
#define RPM_SIGNATURE_MAGIC_DESC       "magic"
#define RPM_SIGNATURE_RESERVED_DESC    "reserved"
#define RPM_SIGNATURE_MAGIC            0x8EADE801
#define RPM_SIGNATURE_RESERVED         0
#define RPM_ENTRY_TAG_DESC             "tag"
#define RPM_ENTRY_TYPE_DESC            "type"
#define RPM_ENTRY_TAGS_DESC            "tags"
#define RPM_ENTRY_VALUE_DESC           "value"
#define RPM_ENTRY_FILE_DESC            "file"
#define RPM_ENTRY_TRAILER_DESC         "trailer"
#define RPM_CHANGELOG_DESC             "changelog"
#define RPM_DEPENDENCIES_DESC          "dependencies"
#define RPM_FILES_DESC                 "files"
#define RPM_DEPENDENCY_NAME_DESC       "name"
#define RPM_DEPENDENCY_FLAGS_DESC      "flags"
#define RPM_DEPENDENCY_COMPARISON_DESC "comparison"
#define RPM_DEPENDENCY_VERSION_DESC    "version"

/* keys used by the entries in the "files" array */
#define RPM_FILE_PATH_DESC             "path"
#define RPM_FILE_SIZE_DESC             "size"
#define RPM_FILE_MODE_DESC             "mode"
#define RPM_FILE_MTIME_DESC            "mtime"
#define RPM_FILE_USER_DESC             "user"
#define RPM_FILE_GROUP_DESC            "group"
#define RPM_FILE_RDEV_DESC             "rdev"
#define RPM_FILE_DEVICE_DESC           "device"
#define RPM_FILE_DIGEST_DESC           "digest"
#define RPM_FILE_LINKTO_DESC           "linkto"
#define RPM_FILE_INODE_DESC            "inode"
#define RPM_FILE_CLASS_DESC            "class"
#define RPM_FILE_LANGS_DESC            "langs"
#define RPM_FILE_COLORS_DESC           "colors"

/* values used by the entries in the "files" array */
#define RPM_FILE_DEFAULT_USER          "root"
#define RPM_FILE_DEFAULT_GROUP         "root"
#define RPM_FILE_CURRENT_DIRECTORY     "./"
#define RPM_FILE_LANG_SEPARATOR        "|"
#define RPM_FILE_MODE_FORMAT           "%04o"
#define RPM_FILE_MTIME_FORMAT          "%Y-%m-%dT%H:%M:%SZ"

/* names used for the file color bits in the "files" array */
#define RPM_FILE_COLOR_ELF32           "Elf32"
#define RPM_FILE_COLOR_ELF64           "Elf64"

/* comparison operator string constants */
#define COMPARISON_LE                  "<="
#define COMPARISON_GE                  ">="
#define COMPARISON_LT                  "<"
#define COMPARISON_GT                  ">"
#define COMPARISON_EQ                  "="

/* general purpose string constants */
#define RPM_METADATA_READ_ONLY         "read-only"

/* sense flag string constants */
#define RPM_SENSE_FLAGS_DESC           "sense_flags"
#define SENSE_FLAG_PRE                 "pre"
#define SENSE_FLAG_POST                "post"
#define SENSE_FLAG_PREUN               "preun"
#define SENSE_FLAG_POSTUN              "postun"
#define SENSE_FLAG_VERIFY              "verify"
#define SENSE_FLAG_INTERP              "interp"
#define SENSE_FLAG_RPMLIB              "rpmlib"
#define SENSE_FLAG_FIND_REQUIRES       "find-requires"
#define SENSE_FLAG_FIND_PROVIDES       "find-provides"
#define SENSE_FLAG_PREREQ              "prereq"
#define SENSE_FLAG_PRETRANS            "pretrans"
#define SENSE_FLAG_POSTTRANS           "posttrans"
#define SENSE_FLAG_PREUNTRANS          "preuntrans"
#define SENSE_FLAG_POSTUNTRANS         "postuntrans"
#define SENSE_FLAG_CONFIG              "config"
#define SENSE_FLAG_MISSINGOK           "missingok"
#define SENSE_FLAG_META                "meta"
#define SENSE_FLAG_TRIGGERIN           "triggerin"
#define SENSE_FLAG_TRIGGERUN           "triggerun"
#define SENSE_FLAG_TRIGGERPOSTUN       "triggerpostun"
#define SENSE_FLAG_TRIGGERPREIN        "triggerprein"
#define SENSE_FLAG_KEYRING             "keyring"

/* Digest types used in the headers */
#define TARPM_DIGEST_MD5               1
#define TARPM_DIGEST_SHA1              2
#define TARPM_DIGEST_SHA256            3
#define TARPM_DIGEST_SHA256_PAYLOAD    4

#endif /* _TARPM_CONSTANTS_H */
