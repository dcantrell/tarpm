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

/* the path meaning stdout rather than a file */
#define OUTPUT_STDOUT                  "-"

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

/*
 * The payload digest algorithm tag.  rpm spells this
 * RPMTAG_PAYLOADDIGESTALGO before 6.0.0 and RPMTAG_PAYLOADSHA256ALGO
 * from 6.0.0 on, but it's 5093 either way.
 */
#define RPMTAG_PAYLOAD_DIGEST_ALGO     5093

/*
 * Signature header tags.  rpm names these in enum rpmSigTag_e in
 * include/rpm/rpmtag.h, but they are enum values and not macros, so
 * we cannot ask the preprocessor whether the release of rpm we build
 * against knows them.  The numbers never change, so we spell them out
 * here and use these instead.  Everything from 256 up is in the
 * signature range (HEADER_SIGBASE in the rpm source) and 999 is the
 * top of that range (HEADER_SIGTOP).
 */
#define RPMSIGTAG_PUBKEYS_VALUE             266
#define RPMSIGTAG_FILESIGNATURES_VALUE      274
#define RPMSIGTAG_FILESIGNATURELENGTH_VALUE 275
#define RPMSIGTAG_VERITYSIGNATURES_VALUE    276
#define RPMSIGTAG_VERITYSIGNATUREALGO_VALUE 277
#define RPMSIGTAG_OPENPGP_VALUE             278
#define RPMSIGTAG_SHA3_256_VALUE            279
#define RPMSIGTAG_RESERVED_VALUE            999

/*
 * The signature header tags rpmsign owns.  These are the tags
 * deleteSigs() and deleteFileSigs() in sign/rpmgensig.cc in the rpm
 * source throw away before rpmsign writes new ones, which makes them
 * the tags only rpmsign can produce.  Making any of them takes the
 * private signing key, so tarpm reads them out of a package it
 * extracts but leaves them out of a package it creates.  Run
 * rpmsign(8) on the new package to put them back.
 */
#define RPMSIGN_SIGNATURE_TAGS               \
    RPMSIGTAG_DSA,                           \
    RPMSIGTAG_RSA,                           \
    RPMSIGTAG_FILESIGNATURES_VALUE,          \
    RPMSIGTAG_FILESIGNATURELENGTH_VALUE,     \
    RPMSIGTAG_VERITYSIGNATURES_VALUE,        \
    RPMSIGTAG_VERITYSIGNATUREALGO_VALUE,     \
    RPMSIGTAG_OPENPGP_VALUE,                 \
    RPMSIGTAG_PGP,                           \
    RPMSIGTAG_GPG,                           \
    RPMSIGTAG_PGP5

/*
 * The RPM package formats we can write a signature header for.  rpm
 * picks between the two with the %_rpmformat macro.
 */
#define RPM_FORMAT_V4                4
#define RPM_FORMAT_V6                6
#define RPM_FORMAT_DEFAULT           RPM_FORMAT_V4

/*
 * The signature header tags rpm writes by default, one group per
 * format.  rpmGenerateSignature() in lib/signature.cc in the rpm
 * source puts these in and nothing else, so we build the same groups
 * rather than carry over whatever a package we extracted happened to
 * hold.  rpmsign(8) adds its own tags afterwards.
 */
#define RPMFORMAT_V4_SIGNATURE_TAGS          \
    HEADER_SIGNATURES,                       \
    RPMSIGTAG_SHA1,                          \
    RPMSIGTAG_SHA256,                        \
    RPMSIGTAG_SIZE,                          \
    RPMSIGTAG_MD5,                           \
    RPMSIGTAG_PAYLOADSIZE,                   \
    RPMSIGTAG_RESERVEDSPACE

#define RPMFORMAT_V6_SIGNATURE_TAGS          \
    HEADER_SIGNATURES,                       \
    RPMSIGTAG_SHA256,                        \
    RPMSIGTAG_SHA3_256_VALUE,                \
    RPMSIGTAG_RESERVED_VALUE

/*
 * The space rpm reserves in the signature header for rpmsign to write
 * in to.  rpm always keeps 32 bytes and adds whatever
 * %__gpg_reserved_space asks for, which is 4096.
 */
#define RPM_SIGNATURE_RESERVED_SIZE    4128

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
#define RPM_DEPENDENCY_COMPARISON_DESC "comparison"
#define RPM_DEPENDENCY_VERSION_DESC    "version"
#define RPM_DEPENDENCY_TYPE_DESC       "type"

/*
 * RPMTAG_BUILDTIME is an int32 in the header, but it is recorded in
 * header.json as an ISO 8601 timestamp in UTC.  This is the same
 * format the "files" array uses for file mtimes.
 */
#define RPM_BUILDTIME_FORMAT           "%Y-%m-%dT%H:%M:%SZ"

/* how the depends dictionary packs a dependency type and index */
/*
 * See rpmfcGenerateDepends() in build/rpmfc.cc in the rpm source.
 * The high byte of each value is the dependency type abbreviation and
 * the remaining three bytes are an index in to that type's dependency
 * array.  Note here that the tag names and code call it a dependency,
 * but this is the provides information which is one part of all of
 * the dependency information in a package.
 */
#define DEPENDS_DICT_TYPE_SHIFT        24
#define DEPENDS_DICT_INDEX_MASK        0x00FFFFFF

/* keys used by the entries in the "files" array */
#define RPM_FILE_PATH_DESC             "path"
#define RPM_FILE_SIZE_DESC             "size"
#define RPM_FILE_MODE_DESC             "mode"
#define RPM_FILE_TYPE_DESC             "type"
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
#define RPM_FILE_FLAGS_DESC            "flags"
#define RPM_FILE_VERIFYFLAGS_DESC      "verifyflags"
#define RPM_FILE_PROVIDES_DESC         "provides"

/* file metadata we can take from the payload tree */
/*
 * Each bit names one value in a "files" entry that we take from the
 * file in the payload tree instead of from header.json.  We pick them
 * up one at a time, so taking the owner does not mean we take the
 * group too.
 */
#define PAYLOAD_OVERRIDE_NONE          0x00
#define PAYLOAD_OVERRIDE_MTIME         0x01
#define PAYLOAD_OVERRIDE_USER          0x02
#define PAYLOAD_OVERRIDE_GROUP         0x04
#define PAYLOAD_OVERRIDE_LINKTO        0x08
#define PAYLOAD_OVERRIDE_ALL           (PAYLOAD_OVERRIDE_MTIME | PAYLOAD_OVERRIDE_USER | PAYLOAD_OVERRIDE_GROUP | PAYLOAD_OVERRIDE_LINKTO)

/* values used by the entries in the "files" array */
/*
 * These values were learned from the rpm source code and seem to be
 * good defaults, probably.
 */
#define RPM_FILE_DEFAULT_USER          "root"
#define RPM_FILE_DEFAULT_GROUP         "root"
#define RPM_FILE_DEFAULT_DEVICE        1
#define RPM_FILE_LANG_SEPARATOR        "|"
#define RPM_FILE_MODE_FORMAT           "%04o"
#define RPM_FILE_MTIME_FORMAT          "%Y-%m-%dT%H:%M:%SZ"

/* names used for the file type bits in the "files" array */
/*
 * RPMTAG_FILEMODES holds the file type bits along with the
 * permissions.  The "mode" key carries the permissions and these
 * names carry the type.  See rpmfiWhatis() in lib/rpmfi.cc in the rpm
 * source for how rpm reads the same bits.
 */
#define RPM_FILE_TYPE_PIPE             "pipe"
#define RPM_FILE_TYPE_CHARDEV          "chardev"
#define RPM_FILE_TYPE_DIR              "dir"
#define RPM_FILE_TYPE_BLOCKDEV         "blockdev"
#define RPM_FILE_TYPE_FILE             "file"
#define RPM_FILE_TYPE_SYMLINK          "symlink"
#define RPM_FILE_TYPE_SOCKET           "socket"

/* class strings rpm writes itself rather than ask libmagic for */
/*
 * See rpmfcClassify() in build/rpmfc.cc in the rpm source.  Anything
 * that is not a regular file or a symlink gets one of these names.
 * Files under /dev/ stand in for real device nodes, so rpm gives them
 * no class at all.  A file libmagic cannot read is called data.
 */
#define RPM_FILE_CLASS_CHARDEV         "character special"
#define RPM_FILE_CLASS_BLOCKDEV        "block special"
#define RPM_FILE_CLASS_PIPE            "fifo (named pipe)"
#define RPM_FILE_CLASS_SOCKET          "socket"
#define RPM_FILE_CLASS_DIR             "directory"
#define RPM_FILE_CLASS_DATA            "data"
#define RPM_FILE_CLASS_NONE            ""
#define RPM_FILE_CLASS_DEV_PREFIX      "/dev/"

/* names used for the file color bits in the "files" array   */
/* Lore:  https://dustymabe.com/2013/08/25/rpm-file-colors/  */
#define RPM_FILE_COLOR_ELF32           "Elf32"
#define RPM_FILE_COLOR_ELF64           "Elf64"

/* names used for the file flag bits in the "files" array */
/*
 * From enum rpmfileAttrs_e in include/rpm/rpmfiles.h in the rpm
 * source; not public API.  New things may show up in rpm!
 */
#define RPM_FILE_FLAG_CONFIG           "config"
#define RPM_FILE_FLAG_DOC              "doc"
#define RPM_FILE_FLAG_ICON             "icon"
#define RPM_FILE_FLAG_MISSINGOK        "missingok"
#define RPM_FILE_FLAG_NOREPLACE        "noreplace"
#define RPM_FILE_FLAG_SPECFILE         "specfile"
#define RPM_FILE_FLAG_GHOST            "ghost"
#define RPM_FILE_FLAG_LICENSE          "license"
#define RPM_FILE_FLAG_README           "readme"
#define RPM_FILE_FLAG_PUBKEY           "pubkey"
#define RPM_FILE_FLAG_ARTIFACT         "artifact"

/* names used for the file verify flag bits in the "files" array */
#define RPM_FILE_VERIFY_FILEDIGEST     "filedigest"
#define RPM_FILE_VERIFY_MD5            "md5"
#define RPM_FILE_VERIFY_FILESIZE       "filesize"
#define RPM_FILE_VERIFY_LINKTO         "linkto"
#define RPM_FILE_VERIFY_USER           "user"
#define RPM_FILE_VERIFY_GROUP          "group"
#define RPM_FILE_VERIFY_MTIME          "mtime"
#define RPM_FILE_VERIFY_MODE           "mode"
#define RPM_FILE_VERIFY_RDEV           "rdev"
#define RPM_FILE_VERIFY_CAPS           "caps"

/* comparison operator string constants */
#define COMPARISON_LE                  "<="
#define COMPARISON_GE                  ">="
#define COMPARISON_LT                  "<"
#define COMPARISON_GT                  ">"
#define COMPARISON_EQ                  "="

/* general purpose string constants */
/*
 * NOTE: This is a marker for tarpm users only and has no bearing on
 * data in an RPM header.  It goes in the JSON output and is meant to
 * tell the tarpm user that what they are looking at is read-only and
 * is not changeable by them.
 */
#define RPM_METADATA_READ_ONLY         "read-only"

/* sense flag string constants */
/*
 * See enum rpmsenseFlags_e include/rpm/rpmds.h in the rpm source?
 * Good luck.
 */
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

/*
 * File digest algorithms.  rpm names these in enum pgpHashAlgo_e in
 * include/rpm/rpmpgp.h, but they are enum values and not macros, so
 * we cannot ask the preprocessor whether the release of rpm we build
 * against knows them.  The SHA3 ones are the only two newer than the
 * oldest rpm we support, so those are the only two we spell out here.
 */
#define PGPHASHALGO_SHA3_256_VALUE     12
#define PGPHASHALGO_SHA3_512_VALUE     14

/* Digest types used in the headers */
#define TARPM_DIGEST_MD5               1
#define TARPM_DIGEST_SHA1              2
#define TARPM_DIGEST_SHA256            3
#define TARPM_DIGEST_SHA256_PAYLOAD    4
#define TARPM_DIGEST_SHA3_256          5

#endif /* _TARPM_CONSTANTS_H */
