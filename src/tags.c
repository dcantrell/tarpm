/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rpm/rpmtag.h>

#include "tarpm.h"

/*
 * Convert tag type to symbolic type name.  Caller must not free the
 * string returned.
 */
const char *
tag_type(rpmTagType type)
{
    switch (type) {
        case RPM_NULL_TYPE:
            return "(null)";
        case RPM_CHAR_TYPE:
            return "char";
        case RPM_INT8_TYPE:
            return "int8";
        case RPM_INT16_TYPE:
            return "int16";
        case RPM_INT32_TYPE:
            return "int32";
        case RPM_INT64_TYPE:
            return "int64";
        case RPM_STRING_TYPE:
            return "string";
        case RPM_BIN_TYPE:
            return "binary blob";
        case RPM_STRING_ARRAY_TYPE:
            return "string array";
        case RPM_I18NSTRING_TYPE:
            return "i18n string";
        default:
            return "(unknown)";
    }
}

/*
 * Convert tag value to symbolic tag name (matches RPM headers).
 * Caller must not free string returned.
 */
const char *
signature_tag_name(rpmSigTag tag)
{
    switch (tag) {
        case RPMSIGTAG_SIZE:
            return "RPMSIGTAG_SIZE";
        case RPMSIGTAG_LEMD5_1:
            return "RPMSIGTAG_LEMD5_1";
        case RPMSIGTAG_PGP:
            return "RPMSIGTAG_PGP";
        case RPMSIGTAG_LEMD5_2:
            return "RPMSIGTAG_LEMD5_2";
        case RPMSIGTAG_MD5:
            return "RPMSIGTAG_MD5";
        case RPMSIGTAG_GPG:
            return "RPMSIGTAG_GPG";
        case RPMSIGTAG_PGP5:
            return "RPMSIGTAG_PGP5";
        case RPMSIGTAG_PAYLOADSIZE:
            return "RPMSIGTAG_PAYLOADSIZE";
        case RPMSIGTAG_RESERVEDSPACE:
            return "RPMSIGTAG_RESERVEDSPACE";
        case RPMSIGTAG_BADSHA1_1:
            return "RPMSIGTAG_BADSHA1_1";
        case RPMSIGTAG_BADSHA1_2:
            return "RPMSIGTAG_BADSHA1_2";
        case RPMSIGTAG_DSA:
            return "RPMSIGTAG_DSA";
        case RPMSIGTAG_RSA:
            return "RPMSIGTAG_RSA";
        case RPMSIGTAG_SHA1:
            return "RPMSIGTAG_SHA1";
        case RPMSIGTAG_LONGSIZE:
            return "RPMSIGTAG_LONGSIZE";
        case RPMSIGTAG_LONGARCHIVESIZE:
            return "RPMSIGTAG_LONGARCHIVESIZE";
        case RPMSIGTAG_SHA256:
            return "RPMSIGTAG_SHA256";
        case RPMSIGTAG_FILESIGNATURES:
            return "RPMSIGTAG_FILESIGNATURES";
        case RPMSIGTAG_FILESIGNATURELENGTH:
            return "RPMSIGTAG_FILESIGNATURELENGTH";
        case RPMSIGTAG_VERITYSIGNATURES:
            return "RPMSIGTAG_VERITYSIGNATURES";
        case RPMSIGTAG_VERITYSIGNATUREALGO:
            return "RPMSIGTAG_VERITYSIGNATUREALGO";
        default:
            return "(unknown)";
    }
}
