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

/*
 * Read the data of the RPM header and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_header_from_rpm(const int fd)
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
