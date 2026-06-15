/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <string.h>
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
valid_header_signature(struct rpmsignature *sig)
{
    if (sig == NULL) {
        return false;
    }

    if (sig->magic != RPM_SIGNATURE_MAGIC) {
        warnx("magic value mismatch, not an RPM");
        return false;
    }

    if (sig->reserved != RPM_SIGNATURE_RESERVED) {
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
    struct rpmsignature *rawsig = NULL;
    struct rpmsigvalues *svals = NULL;
    struct rpmidxentry *entry = NULL;
    struct rpmidxentry *trailer = NULL;
    struct json_object *jvals = NULL;
    struct json_object *header = NULL;

    if (fd <= 0) {
        return NULL;
    }

    /* read in the signature */
    rawsig = read_header_signature(fd);

    if (rawsig == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    svals = compute_sigvalues(rawsig, true);

    /* read in the entries */
    buffer = read_header_entries(fd, rawsig, svals->hlen);
    svals->estart = (struct rpmidxentry *) &(buffer[2]);
    svals->datastart = (uint8_t *) (svals->estart + rawsig->nentries);

    /* first entry */
    entry = (struct rpmidxentry *) (buffer + 2);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(entry, svals->datastart);

    /* generate a JSON structure for the signature */
    header = generate_json(rawsig, svals);

    /* dump all of the tags in the signature */
    jvals = generate_json_entries(rawsig, svals, entry, false);

    /* write the signature to a file */
    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    /* cleanup */
    free(svals);
    json_object_put(jvals);
    free(trailer);
    free(buffer);
    free(rawsig);

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
 * passed in to structures the caller can use for the rpmsignature and
 * rpmsigvalues.  Those will be allocated and modified by this
 * function.  Caller must free memory associated with those
 * structures.
 */
int
create_header(const struct json_object *data, struct rpmsignature **signature, struct rpmsigvalues **sigvalues)
{
    int r = 0;
    size_t i = 0;
    struct rpmsignature *s = *signature;
    struct rpmsigvalues *v = *sigvalues;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;

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
        return -1;
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** create_header: tags must be an array"));
        return -1;
    }

    /* number of header index entries */
    s->nentries = json_object_array_length(tags);

    /* allocate an array for the header index entries */
    v->estart = xcalloc(s->nentries, sizeof(*(v->estart)));
    v->entry = v->estart;
    assert(v->estart != NULL);

    /* walk the header tags and add them to the values structure */
    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (json_object_object_get_ex(entry, "number", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'number'"));
        } else {
            v->entry->tag = json_object_get_int(entry);
        }

        if (json_object_object_get_ex(entry, "type", &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'type'"));
        } else {
            v->entry->type = tag_type(key);
        }





/*


      "offset": "0x10A4",
      "count": "16",
      "value": "AAAAPgAAAAf///+QAAAAEA==\n"




    {
      "name": "RPMSIGTAG_SHA1",
      "number": "269",
      "type": "string",
      "offset": "0x0",
      "count": "1",
      "value": "a786742fedf70b74401955b18c07bf0ad79cf9d5"
    },
    {
      "name": "RPMSIGTAG_SHA256",
      "number": "273",
      "type": "string",
      "offset": "0x29",
      "count": "1",
      "value": "d7f406002d9dd8f2339e0cfa64eebadca12fbdbe6cd7840f88f967f70dcc1f5d"
    },

*/


    }



    return r;
}
