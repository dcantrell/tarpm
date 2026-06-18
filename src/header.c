/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmtd.h>
#include <json.h>

#include "tarpm.h"

/* the header magic and reserved bytes -- from librpm source */
const unsigned char rpm_header_magic[8] = {
    0x8e, 0xad, 0xe8, 0x01, 0x0, 0x00, 0x0, 0x0
};

/*
 * Validates an RPM header signature.  True if valid, false if sig is NULL or sig is invalid.
 */
bool
valid_header_signature(struct rpmhdr *hdr)
{
    if (hdr == NULL) {
        return false;
    }

    if (hdr->magic != RPM_SIGNATURE_MAGIC) {
        warnx("magic value mismatch, not an RPM");
        return false;
    }

    if (hdr->reserved != RPM_SIGNATURE_RESERVED) {
        warnx("reserved value mismatch, not an RPM");
        return false;
    }

    return true;
}

/*
 * Read the data of the RPM header and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_header(const int fd)
{
    uint32_t *buffer = NULL;
    struct rpmhdr *rawhdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *jvals = NULL;
    struct json_object *header = NULL;

    if (fd < 0) {
        return NULL;
    }

    /* read in the signature */
    rawhdr = read_header_signature(fd);

    if (rawhdr == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    hdrinfo = compute_hdrinfo(rawhdr, false);

    /* read in the entries */
    buffer = read_header_entries(fd, rawhdr, hdrinfo->hlen);
    hdrinfo->estart = (struct rpmhdrentry *) &(buffer[2]);
    hdrinfo->datastart = (uint8_t *) (hdrinfo->estart + rawhdr->nentries);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(hdrinfo->estart, hdrinfo->datastart);

    /* generate a JSON structure for the signature */
    header = generate_json(rawhdr, hdrinfo);

    /* dump all of the tags in the signature */
    jvals = generate_json_entries(rawhdr, hdrinfo, trailer, false);

    /* write the signature to a file */
    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    /* cleanup */
    free(hdrinfo);
    json_object_put(jvals);
    free(trailer);
    free(buffer);
    free(rawhdr);

    return header;
}

/*
 * Create a new header data structure for later writing to an RPM
 * output file.  Headers begin with the magic number, number of
 * records, and the size of the storage area.  The storage area is a
 * buffer of header index structs followed by the storage area for
 * that index record.  This function is really more of an
 * initialization of the header structure that you then follow up with
 * functions to add records to.
 *
 * Returns 0 on success, non-zero otherwise.  Pointers should be
 * passed in to structures the caller can use for the rpmhdr and
 * rpmhdrinfo.  Those will be allocated and modified by this function.
 * Caller must free memory associated with those structures.
 */
int
create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo)
{
    int r = 0;
    size_t i = 0;
    struct rpmhdr *s = *hdr;
    struct rpmhdrinfo *v = *hdrinfo;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    uint8_t *datapos = NULL;
    int32_t offset = 0;
    const char *value = NULL;
    int len = 0;

    if (data == NULL) {
        return 0;
    }

    /* allocate the two structures for the header */
    s = xalloc(sizeof(*s));
    assert(s != NULL);

    v = xalloc(sizeof(*v));
    assert(v != NULL);

    /* fill out the beginning with the magic and reserved values */
    s->magic = RPM_SIGNATURE_MAGIC;
    s->reserved = RPM_SIGNATURE_RESERVED;

    /* get the tags for this header */
    if (json_object_object_get_ex(data, "tags", &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        free(s);
        free(v);
        return -1;
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** create_header: tags must be an array"));
        free(s);
        free(v);
        return -1;
    }

    /* number of header index entries */
    s->nentries = json_object_array_length(tags);

    /* allocate an array for the header index entries */
    v->estart = xcalloc(s->nentries, sizeof(*(v->estart)));
    v->entry = v->estart;
    assert(v->estart != NULL);

    /* calculate total data size needed */
    size_t total_data_size = 0;
    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);
        if (json_object_object_get_ex(entry, "value", &key)) {
            total_data_size += json_object_get_string_len(key) + 1;
        }
    }

    /* allocate the data buffer */
    v->datastart = xcalloc(total_data_size, sizeof(uint8_t));
    assert(v->datastart != NULL);

    /* set the data size in the header */
    s->nbytes = total_data_size;

    /* position the data buffer and offset */
    datapos = v->datastart;
    offset = 0;

    /* walk the header tags and add them to the values structure */
    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        /* gather the number, type, and count */
        if (json_object_object_get_ex(entry, "number", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'number'"));
        } else {
            v->entry->tag = json_object_get_int(key);
        }

        if (json_object_object_get_ex(entry, "type", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'type'"));
        } else {
            v->entry->type = tag_type(key);
        }

        if (json_object_object_get_ex(entry, "count", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'count'"));
        } else {
            v->entry->count = json_object_get_int(key);
        }

        /* the offset is computed by us, so write that */
        v->entry->offset = offset;

        /* now get the data and put it in the buffer and update the offset */
        if (json_object_object_get_ex(entry, "value", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'value'"));
        } else {
            value = json_object_get_string(key);
            len = json_object_get_string_len(key);

            memcpy(datapos, value, len);
            datapos += len + 1;
            offset += len + 1;
        }

        /* move to next entry */
        v->entry++;
    }

    *hdr = s;
    *hdrinfo = v;

    return r;
}
