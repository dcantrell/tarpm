/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <err.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>

#include "tarpm.h"

/*
 * Read the data of the RPM signature and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_signature(const int fd)
{
    uint32_t *buffer = NULL;
    struct rpmhdr *rawhdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *jvals = NULL;
    struct json_object *signature = NULL;

    if (fd < 0) {
        return NULL;
    }

    /* read in the signature */
    rawhdr = read_header_signature(fd);

    if (rawhdr == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    hdrinfo = compute_hdrinfo(rawhdr, true);

    /* read in the entries */
    buffer = read_header_entries(fd, rawhdr, hdrinfo->hlen);
    hdrinfo->estart = (struct rpmhdrentry *) &(buffer[2]);
    hdrinfo->datastart = (uint8_t *) (hdrinfo->estart + rawhdr->nentries);

    /* signature is aligned, so padding may be present */
    if (!xread(fd, &hdrinfo->pad, hdrinfo->padlen)) {
        free(buffer);
        free(hdrinfo);
        free(rawhdr);
        return NULL;
    }

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(hdrinfo->estart, hdrinfo->datastart);

    /* generate a JSON structure for the signature */
    signature = generate_json(rawhdr, hdrinfo);

    /* dump all of the tags in the signature */
    jvals = generate_json_entries(rawhdr, hdrinfo, trailer, true);

    /* write the signature to a file */
    json_object_object_add(signature, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    /* cleanup */
    free(hdrinfo);
    json_object_put(jvals);
    free(trailer);
    free(buffer);
    free(rawhdr);

    return signature;
}
