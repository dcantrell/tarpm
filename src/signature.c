/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmbase64.h>
#include <json.h>

#include "tarpm.h"

static void
add_entry_value(struct json_object *arrayentry, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count)
{
    uint8_t *data = NULL;
    union datatypes dt;
    void *blob = NULL;
    char *s = NULL;

    assert(arrayentry != NULL);
    assert(buffer != NULL);

    /* move to the position of this entry's data */
    data = buffer + offset;

    /* read the value */
    switch (datatype) {
        case RPM_NULL_TYPE:
            s = strdup("(null)");
            break;
        case RPM_CHAR_TYPE:
            memcpy(&dt.c, data, sizeof(dt.c));
            xasprintf(&s, "%c", dt.c);
            break;
        case RPM_INT8_TYPE:
            memcpy(&dt.i8, data, sizeof(dt.i8));
            xasprintf(&s, "%d", dt.i8);
            break;
        case RPM_INT16_TYPE:
            memcpy(&dt.i16, data, sizeof(dt.i16));
            dt.i16 = ntohl(dt.i16);
            xasprintf(&s, "%d", dt.i16);
            break;
        case RPM_INT32_TYPE:
            memcpy(&dt.i32, data, sizeof(dt.i32));
            dt.i32 = ntohl(dt.i32);
            xasprintf(&s, "%d", dt.i32);
            break;
        case RPM_INT64_TYPE:
            memcpy(&dt.i64, data, sizeof(dt.i64));
            dt.i64 = ntohl(dt.i64);
            xasprintf(&s, "%ld", dt.i64);
            break;
        case RPM_STRING_TYPE:
            s = strdup((char *) data);
            break;
        case RPM_BIN_TYPE:
            blob = xalloc(count);
            assert(blob != NULL);
            memcpy(blob, data, count);
            s = rpmBase64Encode(blob, count, -1);
            free(blob);

            if (s == NULL) {
                err(EXIT_FAILURE, "rpmBase64Encode");
            }

            break;
        case RPM_STRING_ARRAY_TYPE:
            /* XXX: return "argv"; */
            break;
        case RPM_I18NSTRING_TYPE:
            s = strdup((char *) data);
            break;
        default:
            s = strdup("(unknown)");
            break;
    }

    /* add the value */
    if (s == NULL) {
        s = strdup("");
        assert(s != NULL);
    }

    json_object_object_add(arrayentry, RPM_SIGNATURE_VALUE_DESC, json_object_new_string(s));
    free(s);

    return;
}

/*
 * Extract the data of the RPM signature and convert it to JSON data.
 * Returns 0 on success, -1 on error.
 */
int
extract_signature(const int fd, const char *output_dir)
{
    uint32_t i = 0;
    uint8_t *datastart = NULL;
    rpmSigTag tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    uint32_t *buffer = NULL;
    struct rpmhdrintro *intro = NULL;
    uint32_t ilen = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    struct rpmhdrintro pad;
    struct rpmhdrentry *estart = NULL;
    struct rpmhdrentry *entry = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *out = NULL;
    struct json_object *vals = NULL;
    struct json_object *arrayentry = NULL;
    char *s = NULL;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* read in the signature */
    intro = read_header_intro(fd);

    if (intro == NULL) {
        err(EXIT_FAILURE, "read_header_intro");
    }

    /* computed from header values */
    ilen = intro->nentries * sizeof(struct rpmhdrentry);
    hlen = ilen + intro->nbytes;

    /* read in the entries */
    buffer = read_header_entries(fd, intro, hlen);

    if (intro == NULL) {
        err(EXIT_FAILURE, "read_header_entries");
    }

    estart = (struct rpmhdrentry *) &(buffer[2]);
    datastart = (uint8_t *) (estart + intro->nentries);

    /* signature is aligned, so padding may be present */
    padlen = (8 - (hlen % 8)) % 8;

    if (read(fd, &pad, padlen) != padlen) {
        err(EXIT_FAILURE, "read");
    }

    /* first entry */
    entry = (struct rpmhdrentry *) (buffer + 2);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(entry, datastart);

    /* generate a JSON structure for the signature */
    out = json_object_new_object();

    xasprintf(&s, "0x%X", intro->magic);
    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%04d", intro->reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", intro->nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", intro->nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    /* dump all of the tags in the signature */
    vals = json_object_new_array();

    for (i = 0; i < intro->nentries; i++) {
        /* create the JSON data */
        tag = ntohl(entry[i].tag);
        offset = ntohl(entry[i].offset);
        datatype = ntohl(entry[i].type);
        count = ntohl(entry[i].count);
        arrayentry = json_object_new_object();

        xasprintf(&s, "%s", signature_tag_name(tag));
        json_object_object_add(arrayentry, RPM_SIGNATURE_NAME_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", tag);
        json_object_object_add(arrayentry, RPM_SIGNATURE_TAG_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%s", tag_type(datatype));
        json_object_object_add(arrayentry, RPM_SIGNATURE_TYPE_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "0x%X", offset);
        json_object_object_add(arrayentry, RPM_SIGNATURE_OFFSET_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", count);
        json_object_object_add(arrayentry, RPM_SIGNATURE_COUNT_DESC, json_object_new_string(s));
        free(s);

        add_entry_value(arrayentry, datastart, offset, datatype, count);
        json_object_array_add(vals, arrayentry);
    }

    json_object_object_add(out, RPM_SIGNATURE_TAGS_DESC, json_object_get(vals));

    /* write the signature to a file */
    if (write_json_file(out, output_dir, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    /* cleanup */
    free_json(out);
    json_object_put(vals);
    free(trailer);
    free(buffer);
    free(intro);

    return 0;
}
