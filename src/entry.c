/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <ctype.h>
#include <err.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <rpm/rpmbase64.h>
#include <json.h>

#include "tarpm.h"

/* Returns true if this is a tag whose value should go to a text file. */
bool
is_file_tag(rpmTagVal tag)
{
    bool r = false;

    switch (tag) {
        case RPMTAG_DESCRIPTION:
        case RPMTAG_PREIN:
        case RPMTAG_POSTIN:
        case RPMTAG_PREUN:
        case RPMTAG_POSTUN:
        case RPMTAG_PRETRANS:
        case RPMTAG_POSTTRANS:
#ifdef _HAS_UNTRANS_TAG
        case RPMTAG_PREUNTRANS:
        case RPMTAG_POSTUNTRANS:
#endif
#ifdef _HAS_SPEC_TAG
        case RPMTAG_SPEC:
#endif
            r = true;
            break;
        default:
            r = false;
            break;
    }

    return r;
}

/* Get the name of the file to write the tag value to.  Caller must free. */
char *
get_tag_filename(rpmTagVal tag, const char *ending)
{
    int i = 0;
    char *r = NULL;

    if (!is_file_tag(tag)) {
        return NULL;
    }

    /* build an output filename */
    if (ending) {
        xasprintf(&r, "%s.%s", rpmTagGetName(tag), ending);
    } else {
        r = xstrdup(rpmTagGetName(tag));
    }

    for (i = 0; r[i] != '\0'; i++) {
        r[i] = tolower(r[i]);
    }

    return r;
}

/*
 * Handle entries that write their data to a file rather than
 * adding it directly to the JSON object.  A translation gets the
 * locale in its file name.
 *
 * Returns the basename of the output file created.  Caller must free
 * this string when done.
 */
static char *
write_entry_value_file(rpmTagVal tag, uint8_t *data, const char *dest_dir, const char *locale)
{
    char *tagname = NULL;
    char *base = NULL;
    FILE *fp = NULL;
    char *path = NULL;
    size_t len = 0;

    if (data == NULL) {
        return NULL;
    }

    /* build an output filename */
    if (locale == NULL) {
        tagname = get_tag_filename(tag, OUTPUT_TXT_ENDING);
    } else {
        base = get_tag_filename(tag, NULL);

        if (base != NULL) {
            xasprintf(&tagname, "%s.%s.%s", base, locale, OUTPUT_TXT_ENDING);
            free(base);
        }
    }

    /* only the tags is_file_tag() names go to a file */
    if (tagname == NULL) {
        warnx(_("*** %s does not go to a file"), rpmTagGetName(tag));
        return NULL;
    }

    if (dest_dir) {
        xasprintf(&path, "%s/%s", dest_dir, tagname);
    } else {
        path = xstrdup(tagname);
    }

    /* create output directory if needed */
    if (mkdirp_for_file(path) == -1) {
        warnx("mkdirp_for_file");
        free(path);
        free(tagname);
        return NULL;
    }

    /* open the output file */
    fp = fopen_output(path);

    if (fp == NULL) {
        free(path);
        free(tagname);
        return NULL;
    }

    len = strlen((char *) data);

    /* len may be 0 which indicates an empty file */
    if (len > 0 && fwrite(data, len, 1, fp) != 1) {
        warn("fwrite");
        fclose(fp);
        free(path);
        free(tagname);
        return NULL;
    }

    if (fclose(fp) != 0) {
        warn("fclose");
        free(path);
        free(tagname);
        return NULL;
    }

    free(path);
    return tagname;
}

/*
 * True if a locale is safe to put in a file name.  The name comes from
 * the package, so we only take what a locale name is made of.
 */
static bool
valid_locale(const char *locale)
{
    size_t i = 0;

    if (locale == NULL || locale[0] == '\0' || locale[0] == '.') {
        return false;
    }

    for (i = 0; locale[i] != '\0'; i++) {
        if (!isalnum((unsigned char) locale[i]) && strchr("_-.@", locale[i]) == NULL) {
            return false;
        }
    }

    return true;
}

/*
 * An i18n string tag holds one string per locale in the order of
 * Headeri18ntable, and may stop short of the end of the table.  Replace
 * the value add_entry_value() wrote for the first one with an object
 * keyed by locale.  Without a locale for every string, or with a locale
 * named twice, we warn and leave it alone since the object would lose
 * a string.  Returns 0 on success, -1 for a locale we cannot use as a
 * file name.
 */
int
add_i18n_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *data, uint32_t count, struct json_object *i18ntable, const char *dest_dir)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint8_t *p = data;
    char *filename = NULL;
    const char *locale = NULL;
    struct json_object *first = NULL;
    struct json_object *values = NULL;

    if (i18ntable == NULL || count > json_object_array_length(i18ntable)) {
        warnx(_("*** the %s tag has translations with no locale in %s, keeping the first string"), rpmTagGetName(tag), rpmTagGetName(RPMTAG_HEADERI18NTABLE));
        return 0;
    }

    /* the table is short, so the pairwise check costs nothing */
    for (i = 1; i < count; i++) {
        locale = json_object_get_string(json_object_array_get_idx(i18ntable, i));

        for (j = 0; j < i; j++) {
            if (!strcmp(locale, json_object_get_string(json_object_array_get_idx(i18ntable, j)))) {
                warnx(_("*** the locale \"%s\" is in %s more than once, keeping the first string of the %s tag"), locale, rpmTagGetName(RPMTAG_HEADERI18NTABLE), rpmTagGetName(tag));
                return 0;
            }
        }
    }

    values = json_object_new_object();

    for (i = 0; i < count; i++) {
        locale = json_object_get_string(json_object_array_get_idx(i18ntable, i));

        if (!is_file_tag(tag)) {
            json_object_object_add(values, locale, json_object_new_string((char *) p));
        } else if (!valid_locale(locale)) {
            warnx(_("*** the locale \"%s\" in %s cannot be used in a file name"), locale, rpmTagGetName(RPMTAG_HEADERI18NTABLE));
            json_object_put(values);
            return -1;
        } else {
            /* add_entry_value() already wrote the first string where a single string goes */
            if (i == 0 && json_object_object_get_ex(arrayentry, RPM_ENTRY_FILE_DESC, &first)) {
                filename = xstrdup(json_object_get_string(first));
            } else if (i == 0) {
                filename = write_entry_value_file(tag, p, dest_dir, NULL);
            } else {
                filename = write_entry_value_file(tag, p, dest_dir, locale);
            }

            if (filename == NULL) {
                json_object_put(values);
                return -1;
            }

            json_object_object_add(values, locale, json_object_new_string(filename));
            free(filename);
        }

        p += strlen((char *) p) + 1;
    }

    if (is_file_tag(tag)) {
        json_object_object_add(arrayentry, RPM_ENTRY_FILE_DESC, values);
    } else {
        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, values);
    }

    return 0;
}

int
add_entry_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count, const char *dest_dir, const bool signature)
{
    uint32_t i = 0;
    uint8_t *data = NULL;
    union datatypes dt;
    void *blob = NULL;
    char *s = NULL;
    uint8_t *p = NULL;
    struct json_object *sa = NULL;
    char *tagname = NULL;

    if (arrayentry == NULL || buffer == NULL) {
        return -1;
    }

    /* move to the position of this entry's data */
    data = buffer + offset;

    /* read the value */
    if (datatype == RPM_NULL_TYPE) {
        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string("(null)"));
    } else if (datatype == RPM_CHAR_TYPE) {
        /* count characters with no NUL after them */
        xasprintf(&s, "%.*s", (int) count, (char *) data);
        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
        free(s);
    } else if (datatype == RPM_INT8_TYPE) {
        if (count == 1) {
            memcpy(&dt.i8, data, sizeof(dt.i8));
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i8));
        } else {
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                memcpy(&dt.i8, p, sizeof(dt.i8));
                json_object_array_add(sa, json_object_new_int(dt.i8));
                p += sizeof(dt.i8);
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
        }
    } else if (datatype == RPM_INT16_TYPE) {
        if (count == 1) {
            memcpy(&dt.i16, data, sizeof(dt.i16));
            dt.i16 = ntohs(dt.i16);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i16));
        } else {
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                memcpy(&dt.i16, p, sizeof(dt.i16));
                dt.i16 = ntohs(dt.i16);
                json_object_array_add(sa, json_object_new_int(dt.i16));
                p += sizeof(dt.i16);
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
        }
    } else if (datatype == RPM_INT32_TYPE) {
        if (!signature && (tag == RPMTAG_FILEDIGESTALGO || tag == RPMTAG_PAYLOAD_DIGEST_ALGO) && count == 1) {
            /* record the digest algorithm by name rather than by number */
            memcpy(&dt.i32, data, sizeof(dt.i32));
            dt.i32 = (int32_t) ntohl(dt.i32);
            s = strdigestalgo((uint32_t) dt.i32);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
            free(s);
        } else if (!signature && tag == RPMTAG_BUILDTIME && count == 1) {
            /* record the build time as a timestamp rather than a number */
            memcpy(&dt.i32, data, sizeof(dt.i32));
            dt.i32 = (int32_t) ntohl(dt.i32);
            s = strbuildtime((uint32_t) dt.i32);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
            free(s);
        } else if (count == 1) {
            memcpy(&dt.i32, data, sizeof(dt.i32));
            dt.i32 = (int32_t) ntohl(dt.i32);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i32));
        } else {
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                memcpy(&dt.i32, p, sizeof(dt.i32));
                dt.i32 = (int32_t) ntohl(dt.i32);
                json_object_array_add(sa, json_object_new_int(dt.i32));
                p += sizeof(dt.i32);
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
        }
    } else if (datatype == RPM_INT64_TYPE) {
        if (count == 1) {
            memcpy(&dt.i64, data, sizeof(dt.i64));
            dt.i64 = be64toh(dt.i64);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int64(dt.i64));
        } else {
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                memcpy(&dt.i64, p, sizeof(dt.i64));
                dt.i64 = be64toh(dt.i64);
                json_object_array_add(sa, json_object_new_int64(dt.i64));
                p += sizeof(dt.i64);
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
        }
    } else if (datatype == RPM_STRING_TYPE || datatype == RPM_I18NSTRING_TYPE) {
        if (is_file_tag(tag)) {
            /* write this tag value to a metadata file rather than a string in the JSON data */
            tagname = write_entry_value_file(tag, data, dest_dir, NULL);

            /* the value is lost without its file, so this is an error */
            if (tagname == NULL) {
                return -1;
            }

            /* add the JSON entry noting it's a file and not a direct value */
            json_object_object_add(arrayentry, RPM_ENTRY_FILE_DESC, json_object_new_string(tagname));
            free(tagname);
        } else {
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string((char *) data));
        }
    } else if (datatype == RPM_BIN_TYPE) {
        blob = xalloc(count);
        memcpy(blob, data, count);
        s = rpmBase64Encode(blob, count, -1);
        free(blob);

        if (s == NULL) {
            warnx(_("*** unable to encode the %s value"), rpmTagGetName(tag));
            return -1;
        }

        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
        free(s);
    } else if (datatype == RPM_STRING_ARRAY_TYPE) {
        sa = json_object_new_array();
        p = data;

        for (i = 0; i < count; i++) {
            json_object_array_add(sa, json_object_new_string((char *) p));
            p += strlen((char *) p) + 1;
        }

        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
    } else {
        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string("(unknown)"));
    }

    return 0;
}
