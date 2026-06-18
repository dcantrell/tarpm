/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <err.h>
#include <rpm/rpmtag.h>

#include "tarpm.h"

/*
 * Convert tag type to symbolic type name.  Caller must not free the
 * string returned.
 */
const char *
strtagtype(rpmTagType type)
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
 * Convert string type name to rpmTagType value.
 */
rpmTagType
tag_type(struct json_object *tag)
{
    const char *s = NULL;

    assert(tag != NULL);

    s = json_object_get_string(tag);

    if (s == NULL) {
        return RPM_NULL_TYPE;
    }

    if (!strcmp(s, "(null)")) {
        return RPM_NULL_TYPE;
    } else if (!strcmp(s, "char")) {
        return RPM_CHAR_TYPE;
    } else if (!strcmp(s, "int8")) {
        return RPM_INT8_TYPE;
    } else if (!strcmp(s, "int16")) {
        return RPM_INT16_TYPE;
    } else if (!strcmp(s, "int32")) {
        return RPM_INT32_TYPE;
    } else if (!strcmp(s, "int64")) {
        return RPM_INT64_TYPE;
    } else if (!strcmp(s, "string")) {
        return RPM_STRING_TYPE;
    } else if (!strcmp(s, "binary blob")) {
        return RPM_BIN_TYPE;
    } else if (!strcmp(s, "string array")) {
        return RPM_STRING_ARRAY_TYPE;
    } else if (!strcmp(s, "i18n string")) {
        return RPM_I18NSTRING_TYPE;
    } else {
        return RPM_NULL_TYPE;
    }
}

/*
 * Convert tag value to symbolic tag name (matches RPM headers).
 * Caller must not free string returned.
 *
 * NOTE: Use uint32_t here and not rpmSigTag since we may get private
 * values that are not in the rpmSigTag enum.
 */
const char *
sig_tag_name(uint32_t tag)
{
    switch (tag) {
        case HEADER_SIGNATURES:
            return "Headersignatures";
        case HEADER_IMMUTABLE:
            return "Headerimmutable";
        case RPMSIGTAG_SIZE:
            return "Size";
        case RPMSIGTAG_LEMD5_1:
            return "Lemd5_1";
        case RPMSIGTAG_PGP:
            return "Pgp";
        case RPMSIGTAG_LEMD5_2:
            return "Lemd5_2";
        case RPMSIGTAG_MD5:
            return "Md5";
        case RPMSIGTAG_GPG:
            return "Gpg";
        case RPMSIGTAG_PGP5:
            return "Pgp5";
        case RPMSIGTAG_PAYLOADSIZE:
            return "Payloadsize";
        case RPMSIGTAG_RESERVEDSPACE:
            return "Reservedspace";
        case RPMSIGTAG_BADSHA1_1:
            return "Badsha1_1";
        case RPMSIGTAG_BADSHA1_2:
            return "Badsha1_2";
        case RPMSIGTAG_DSA:
            return "Dsa";
        case RPMSIGTAG_RSA:
            return "Rsa";
        case RPMSIGTAG_SHA1:
            return "Sha1";
        case RPMSIGTAG_LONGSIZE:
            return "Longsize";
        case RPMSIGTAG_LONGARCHIVESIZE:
            return "Longarchivesize";
        case RPMSIGTAG_SHA256:
            return "Sha256";
        case RPMSIGTAG_FILESIGNATURES:
            return "Filesignatures";
        case RPMSIGTAG_FILESIGNATURELENGTH:
            return "Filesignaturelength";
        case RPMSIGTAG_VERITYSIGNATURES:
            return "Veritysignatures";
        case RPMSIGTAG_VERITYSIGNATUREALGO:
            return "Veritysignaturealgo";
        default:
            return "(unknown)";
    }
}

/*
 * Given a json_object representing a "tags" array from a header JSON
 * file, search for the array entry where the "name" field matches the
 * name parameter on this function.  Return the tag value as a string
 * or NULL if not found.  Caller must not free the returned string.
 */
const char *
get_tag_value(const struct json_object *tags, const char *name)
{
    const char *v = NULL;
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (tags == NULL || name == NULL) {
        return NULL;
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** get_tag_value: tags must be an array"));
        return NULL;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        value = NULL;
        entry = json_object_array_get_idx(tags, i);

        if (entry == NULL) {
            break;
        }

        if (json_object_object_get_ex(entry, "name", &value) == 1) {
            /* we found the name key, check the value */
            if (strcmp(name, json_object_get_string(value))) {
                /* no match */
                continue;
            }

            value = NULL;

            /* we have a match, get the value for the caller */
            if (json_object_object_get_ex(entry, "value", &value) == 1) {
                v = json_object_get_string(value);
                break;
            }
        }
    }

    return v;
}
