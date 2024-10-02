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
    struct rpmsighdr sig;
    uint32_t ilen = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    struct rpmsighdr pad;
    struct rpmhdrentry *estart = NULL;
    struct rpmhdrentry *entry = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *out = NULL;
    struct json_object *vals = NULL;
    struct json_object *arrayentry = NULL;
    char *s = NULL;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* zero out the structures */
    memset(&sig, 0, sizeof(sig));

    /* read in the signature */
    if (read(fd, &sig, RPMSIGHDRSZ) != RPMSIGHDRSZ) {
        err(EXIT_FAILURE, "read");
    }

    sig.magic = ntohl(sig.magic);
    sig.nentries = ntohl(sig.nentries);
    sig.nbytes = ntohl(sig.nbytes);

    /* computed from header values */
    ilen = sig.nentries * sizeof(entry);
    hlen = ilen + sig.nbytes;

    /* read in entries */
    /* (largely from rpmdump.c) */
    buffer = xcalloc(sig.nentries, sig.nbytes + hlen);
    assert(buffer != NULL);

    buffer[0] = htonl(sig.nentries);
    buffer[1] = htonl(sig.nbytes);

    estart = (struct rpmhdrentry *) &(buffer[2]);
    datastart = (uint8_t *) (estart + sig.nentries);

    if (read(fd, buffer + 2, hlen) != hlen) {
        err(EXIT_FAILURE, "read");
    }

    /* signature is aligned, so padding may be present */
    padlen = (8 - (hlen % 8)) % 8;

    if (read(fd, &pad, padlen) != padlen) {
        err(EXIT_FAILURE, "read");
    }

    /* verify the magic and reserved values are correct */
    if (sig.magic != RPM_SIGNATURE_MAGIC) {
        err(EXIT_FAILURE, "magic value mismatch, not an RPM");
    }

    if (sig.reserved != 0) {
        err(EXIT_FAILURE, "reserved value mismatch, not an RPM");
    }

    /* generate a JSON structure for the signature */
    out = json_object_new_object();

    xasprintf(&s, "0x%X", sig.magic);
    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%04d", sig.reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig.nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig.nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    /* dump all of the tags in the signature */
    vals = json_object_new_array();

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    /* from rpmdump.c in rpm's source */
    entry = (struct rpmhdrentry *) (buffer + 2);
    tag = ntohl(entry->tag);
    datastart = (uint8_t *) (estart + sig.nentries);

    if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {
        trailer = xalloc(sizeof(*trailer));
        assert(trailer != NULL);
        memcpy(trailer, datastart + ntohl(entry->offset), sizeof(*trailer));
    }

    for (i = 0; i < sig.nentries; i++) {
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

    return 0;
}
