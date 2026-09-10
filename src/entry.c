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
        r = strdup(rpmTagGetName(tag));
    }

    assert(r != NULL);

    for (i = 0; r[i] != '\0'; i++) {
        r[i] = tolower(r[i]);
    }

    return r;
}

/*
 * Handle entries that write their data to a file rather than
 * adding it directly to the JSON object.
 *
 * Returns the basename of the output file created.  Caller must free
 * this string when done.
 */
static char *
write_entry_value_file(rpmTagVal tag, uint8_t *data, const char *dest_dir)
{
    char *tagname = NULL;
    FILE *fp = NULL;
    char *path = NULL;
    size_t len = 0;

    if (data == NULL) {
        return NULL;
    }

    /* build an output filename */
    tagname = get_tag_filename(tag, OUTPUT_TXT_ENDING);

    if (dest_dir) {
        xasprintf(&path, "%s/%s", dest_dir, tagname);
    } else {
        path = strdup(tagname);
    }

    /* open the output file */
    fp = fopen(path, "wb");

    if (fp == NULL) {
        warn("fopen");
        free(path);
        free(tagname);
        return NULL;
    }

    len = strlen((char *) data);

    if (fwrite(data, len, 1, fp) == 0) {
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

void
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
        return;
    }

    /* move to the position of this entry's data */
    data = buffer + offset;

    /* read the value */
    if (datatype == RPM_NULL_TYPE) {
        json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string("(null)"));
    } else if (datatype == RPM_CHAR_TYPE) {
        memcpy(&dt.c, data, sizeof(dt.c));
        xasprintf(&s, "%c", dt.c);
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
            dt.i16 = (int16_t) ntohs(dt.i16);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i16));
        } else {
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                memcpy(&dt.i16, p, sizeof(dt.i16));
                dt.i16 = (int16_t) ntohs(dt.i16);
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
            tagname = write_entry_value_file(tag, data, dest_dir);

            /* add the JSON entry noting it's a file and not a direct value */
            if (tagname != NULL) {
                json_object_object_add(arrayentry, RPM_ENTRY_FILE_DESC, json_object_new_string(tagname));
                free(tagname);
            }
        } else {
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string((char *) data));
        }
    } else if (datatype == RPM_BIN_TYPE) {
        blob = xalloc(count);
        memcpy(blob, data, count);
        s = rpmBase64Encode(blob, count, -1);
        free(blob);

        if (s == NULL) {
            err(EXIT_FAILURE, "rpmBase64Encode");
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

    return;
}
