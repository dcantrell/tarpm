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
    uint32_t *buffer = NULL;
    struct rpmsignature *sig = NULL;
    struct rpmsigvalues *svals = NULL;
    struct rpmidxentry *entry = NULL;
    struct rpmidxentry *trailer = NULL;
    struct json_object *out = NULL;
    struct json_object *jvals = NULL;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* read in the signature */
    sig = read_header_signature(fd);

    if (sig == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    svals = compute_sigvalues(sig, true);

    /* read in the entries */
    buffer = read_header_entries(fd, sig, svals->hlen);
    svals->estart = (struct rpmidxentry *) &(buffer[2]);
    svals->datastart = (uint8_t *) (svals->estart + sig->nentries);

    /* signature is aligned, so padding may be present */
    if (read(fd, &svals->pad, svals->padlen) != svals->padlen) {
        err(EXIT_FAILURE, "read");
    }

    /* first entry */
    entry = (struct rpmidxentry *) (buffer + 2);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(entry, svals->datastart);

    /* generate a JSON structure for the signature */
    out = generate_json(sig, svals);

    /* dump all of the tags in the signature */
    jvals = generate_json_entries(sig, svals, entry, true);

    /* write the signature to a file */
    json_object_object_add(out, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    if (write_json_file(out, output_dir, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    /* cleanup */
    free_json(out);
    json_object_put(jvals);
    free(trailer);
    free(buffer);
    free(sig);

    return 0;
}
