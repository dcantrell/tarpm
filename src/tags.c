/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <err.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmpgp.h>

#include "tarpm.h"

/*
 * Known file digest algorithms.  The names are the PGPHASHALGO_*
 * constants from librpm (include/rpm/rpmpgp.h) turned in to
 * human-readable strings.  We would use the RPM_HASH_* constants from
 * rpmcrypto.h, but those duplicate the rpmpgp.h values and are not
 * available on older releases of rpm which we still want to support.
 * The SHA3 algorithms are newer than the oldest rpm we support, so we
 * carry those two numbers ourselves.
 */
static struct {
    uint32_t algo;
    const char *name;
} digest_algos[] = {
    { PGPHASHALGO_MD5, "md5" },
    { PGPHASHALGO_SHA1, "sha1" },
    { PGPHASHALGO_RIPEMD160, "ripemd160" },
    { PGPHASHALGO_MD2, "md2" },
    { PGPHASHALGO_TIGER192, "tiger192" },
    { PGPHASHALGO_HAVAL_5_160, "haval-5-160" },
    { PGPHASHALGO_SHA256, "sha256" },
    { PGPHASHALGO_SHA384, "sha384" },
    { PGPHASHALGO_SHA512, "sha512" },
    { PGPHASHALGO_SHA224, "sha224" },
    { PGPHASHALGO_SHA3_256_VALUE, "sha3-256" },
    { PGPHASHALGO_SHA3_512_VALUE, "sha3-512" },
    { 0, NULL }
};

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

    if (tag == NULL) {
        return RPM_NULL_TYPE;
    }

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
 * Convert a file digest algorithm number to its name.  Algorithms
 * that have no known name are returned as the number itself in string
 * form.  Caller must free the string returned.
 */
char *
strdigestalgo(uint32_t algo)
{
    int i = 0;
    char *s = NULL;

    for (i = 0; digest_algos[i].name != NULL; i++) {
        if (digest_algos[i].algo == algo) {
            s = strdup(digest_algos[i].name);

            if (s == NULL) {
                err(EXIT_FAILURE, "strdup");
            }

            return s;
        }
    }

    /* no name for this algorithm, so use the number */
    xasprintf(&s, "%u", algo);
    return s;
}

/*
 * Convert a file digest algorithm name back to its number.  Names
 * that are not known are read as a number, which is how
 * strdigestalgo() writes out algorithms it has no name for.  Returns
 * zero if the name is neither known nor a number.
 */
uint32_t
digest_algo(const char *name)
{
    int i = 0;
    char *end = NULL;
    unsigned long value = 0;

    if (name == NULL) {
        return 0;
    }

    for (i = 0; digest_algos[i].name != NULL; i++) {
        if (!strcmp(digest_algos[i].name, name)) {
            return digest_algos[i].algo;
        }
    }

    errno = 0;
    value = strtoul(name, &end, 10);

    if (errno != 0 || end == name || *end != '\0') {
        warnx(_("*** unknown file digest algorithm: %s"), name);
        return 0;
    }

    return (uint32_t) value;
}

/*
 * Convert a build time to an ISO 8601 timestamp in UTC, which is how
 * header.json records it.  Values that will not convert are returned
 * as the number itself in string form.  Caller must free the string
 * returned.
 */
char *
strbuildtime(uint32_t buildtime)
{
    time_t t = 0;
    struct tm tm_info;
    char buf[64];
    char *s = NULL;

    t = (time_t) buildtime;
    memset(&tm_info, 0, sizeof(tm_info));
    memset(buf, 0, sizeof(buf));

    if (gmtime_r(&t, &tm_info) == NULL || strftime(buf, sizeof(buf), RPM_BUILDTIME_FORMAT, &tm_info) == 0) {
        /* no timestamp for this value, so use the number */
        xasprintf(&s, "%u", buildtime);
        return s;
    }

    s = strdup(buf);

    if (s == NULL) {
        err(EXIT_FAILURE, "strdup");
    }

    return s;
}

/*
 * Convert an ISO 8601 timestamp back to a build time.  Strings that
 * are not timestamps are read as a number, which is how
 * strbuildtime() writes out values it could not convert.  Returns
 * zero if the string is neither.
 */
uint32_t
buildtime_value(const char *timestamp)
{
    struct tm tm_info;
    char *end = NULL;
    unsigned long value = 0;

    if (timestamp == NULL) {
        return 0;
    }

    memset(&tm_info, 0, sizeof(tm_info));

    if (strptime(timestamp, RPM_BUILDTIME_FORMAT, &tm_info) != NULL) {
        return (uint32_t) timegm(&tm_info);
    }

    errno = 0;
    value = strtoul(timestamp, &end, 10);

    if (errno != 0 || end == timestamp || *end != '\0') {
        warnx(_("*** unknown build time: %s"), timestamp);
        return 0;
    }

    return (uint32_t) value;
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
        case RPMSIGTAG_PUBKEYS_VALUE:
            return "Pubkeys";
        case RPMSIGTAG_FILESIGNATURES_VALUE:
            return "Filesignatures";
        case RPMSIGTAG_FILESIGNATURELENGTH_VALUE:
            return "Filesignaturelength";
        case RPMSIGTAG_VERITYSIGNATURES_VALUE:
            return "Veritysignatures";
        case RPMSIGTAG_VERITYSIGNATUREALGO_VALUE:
            return "Veritysignaturealgo";
        case RPMSIGTAG_OPENPGP_VALUE:
            return "Openpgp";
        case RPMSIGTAG_SHA3_256_VALUE:
            return "Sha3_256";
        case RPMSIGTAG_RESERVED_VALUE:
            return "Reserved";
        default:
            return "(unknown)";
    }
}

static uint32_t
sig_tag_number(const char *tag)
{
    char *endptr = NULL;
    long tag_num = 0;

    if (tag == NULL) {
        return 0;
    }

    /* Handle numeric tags in "#XXXX" format */
    if (tag[0] == '#') {
        errno = 0;
        tag_num = strtol(tag + 1, &endptr, 10);

        if (errno == 0 && endptr != (tag + 1) && *endptr == '\0' && tag_num > 0) {
            return (uint32_t) tag_num;
        }

        return 0;
    }

    if (!strcmp(tag, "Headersignatures")) {
        return HEADER_SIGNATURES;
    } else if (!strcmp(tag, "Headerimmutable")) {
        return HEADER_IMMUTABLE;
    } else if (!strcmp(tag, "Size")) {
        return RPMSIGTAG_SIZE;
    } else if (!strcmp(tag, "Lemd5_1")) {
        return RPMSIGTAG_LEMD5_1;
    } else if (!strcmp(tag, "Pgp")) {
        return RPMSIGTAG_PGP;
    } else if (!strcmp(tag, "Lemd5_2")) {
        return RPMSIGTAG_LEMD5_2;
    } else if (!strcmp(tag, "Md5")) {
        return RPMSIGTAG_MD5;
    } else if (!strcmp(tag, "Gpg")) {
        return RPMSIGTAG_GPG;
    } else if (!strcmp(tag, "Pgp5")) {
        return RPMSIGTAG_PGP5;
    } else if (!strcmp(tag, "Payloadsize")) {
        return RPMSIGTAG_PAYLOADSIZE;
    } else if (!strcmp(tag, "Reservedspace")) {
        return RPMSIGTAG_RESERVEDSPACE;
    } else if (!strcmp(tag, "Badsha1_1")) {
        return RPMSIGTAG_BADSHA1_1;
    } else if (!strcmp(tag, "Badsha1_2")) {
        return RPMSIGTAG_BADSHA1_2;
    } else if (!strcmp(tag, "Dsa")) {
        return RPMSIGTAG_DSA;
    } else if (!strcmp(tag, "Rsa")) {
        return RPMSIGTAG_RSA;
    } else if (!strcmp(tag, "Sha1")) {
        return RPMSIGTAG_SHA1;
    } else if (!strcmp(tag, "Longsize")) {
        return RPMSIGTAG_LONGSIZE;
    } else if (!strcmp(tag, "Longarchivesize")) {
        return RPMSIGTAG_LONGARCHIVESIZE;
    } else if (!strcmp(tag, "Sha256")) {
        return RPMSIGTAG_SHA256;
    } else if (!strcmp(tag, "Pubkeys")) {
        return RPMSIGTAG_PUBKEYS_VALUE;
    } else if (!strcmp(tag, "Filesignatures")) {
        return RPMSIGTAG_FILESIGNATURES_VALUE;
    } else if (!strcmp(tag, "Filesignaturelength")) {
        return RPMSIGTAG_FILESIGNATURELENGTH_VALUE;
    } else if (!strcmp(tag, "Veritysignatures")) {
        return RPMSIGTAG_VERITYSIGNATURES_VALUE;
    } else if (!strcmp(tag, "Veritysignaturealgo")) {
        return RPMSIGTAG_VERITYSIGNATUREALGO_VALUE;
    } else if (!strcmp(tag, "Openpgp")) {
        return RPMSIGTAG_OPENPGP_VALUE;
    } else if (!strcmp(tag, "Sha3_256")) {
        return RPMSIGTAG_SHA3_256_VALUE;
    } else if (!strcmp(tag, "Reserved")) {
        return RPMSIGTAG_RESERVED_VALUE;
    } else {
        return 0;
    }
}

/* Return the tag number given the tag name */
rpmTagVal
get_tag_number(struct json_object *entry, bool signature)
{
    rpmTagVal t = 0;
    uint32_t sigtag = 0;
    struct json_object *tag = NULL;
    const char *tagname = NULL;

    if (entry == NULL) {
        return RPMTAG_NOT_FOUND;
    }

    if (!json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tag)) {
        warnx("json_object_object_get_ex");
        return RPMTAG_NOT_FOUND;
    }

    tagname = json_object_get_string(tag);

    /*
     * For signature tags, check signature tag names first to
     * avoid conflicts with header tags
     */
    if (signature) {
        sigtag = sig_tag_number(tagname);

        if (sigtag != 0) {
            return sigtag;
        }
    }

    t = rpmTagGetValue(tagname);

    if (t == RPMTAG_NOT_FOUND) {
        return sig_tag_number(tagname);
    } else {
        return t;
    }
}

/*
 * Given a "tags" array from a header JSON file, find the entry whose
 * "tag" field matches name and return its value as a string.  Returns
 * NULL if there is no match.  Caller must not free the string.
 */
const char *
get_tag_value(const struct json_object *tags, const char *tag)
{
    const char *v = NULL;
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (tags == NULL || tag == NULL) {
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

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &value) == 1) {
            /* we found the name key, check the value */
            if (strcmp(tag, json_object_get_string(value))) {
                /* no match */
                continue;
            }

            value = NULL;

            /* we have a match, get the value for the caller */
            if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value) == 1) {
                v = json_object_get_string(value);
                break;
            }
        }
    }

    return v;
}

/*
 * Given a "tags" array from a header JSON file, find the entry whose
 * "tag" field matches name and set its value to new_value.  Returns 0
 * on success, -1 on failure.
 */
int
set_tag_value(struct json_object *tags, const char *tag, const char *new_value)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (tags == NULL || tag == NULL || new_value == NULL) {
        return -1;
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** set_tag_value: tags must be an array"));
        return -1;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        value = NULL;
        entry = json_object_array_get_idx(tags, i);

        if (entry == NULL) {
            break;
        }

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &value) == 1) {
            if (!strcmp(tag, json_object_get_string(value))) {
                json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(new_value));
                return 0;
            }
        }
    }

    return -1;
}
