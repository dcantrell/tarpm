/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <err.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>

#include "tarpm.h"

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
    struct rpmsignature *sig = NULL;
    uint32_t ilen = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    struct rpmsignature pad;
    struct rpmidxentry *estart = NULL;
    struct rpmidxentry *entry = NULL;
    struct rpmidxentry *trailer = NULL;
    struct json_object *out = NULL;
    struct json_object *vals = NULL;
    struct json_object *arrayentry = NULL;
    char *s = NULL;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* read in the signature */
    sig = read_header_signature(fd);

    if (sig == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    ilen = sig->nentries * sizeof(struct rpmidxentry);
    hlen = ilen + sig->nbytes;

    /* read in the entries */
    buffer = read_header_entries(fd, sig, hlen);

    if (sig == NULL) {
        err(EXIT_FAILURE, "read_header_entries");
    }

    estart = (struct rpmidxentry *) &(buffer[2]);
    datastart = (uint8_t *) (estart + sig->nentries);

    /* signature is aligned, so padding may be present */
    padlen = (8 - (hlen % 8)) % 8;

    if (read(fd, &pad, padlen) != padlen) {
        err(EXIT_FAILURE, "read");
    }

    /* first entry */
    entry = (struct rpmidxentry *) (buffer + 2);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(entry, datastart);

    /* generate a JSON structure for the signature */
    out = json_object_new_object();

    xasprintf(&s, "0x%X", sig->magic);
    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%04d", sig->reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig->nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig->nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    /* dump all of the tags in the signature */
    vals = json_object_new_array();

    for (i = 0; i < sig->nentries; i++) {
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
    free(sig);

    return 0;
}
