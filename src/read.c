/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <unistd.h>
#include <err.h>
#include <arpa/inet.h>
#include "tarpm.h"

/*
 * Given a "signature" or "header" header, compute the values necessary
 * to iterate over it.  Return the computed values as a struct that the
 * caller must free.
 */
struct rpmsigvalues *
compute_sigvalues(const struct rpmsignature *sig, const bool signature)
{
    struct rpmsigvalues *vals = NULL;

    if (sig == NULL) {
        return NULL;
    }

    vals = calloc(1, sizeof(*vals));
    assert(vals != NULL);

    /* computed from header values */
    vals->ilen = sig->nentries * sizeof(struct rpmidxentry);
    vals->hlen = vals->ilen + sig->nbytes;

    /* signature is aligned, so padding may be present */
    if (signature) {
        vals->padlen = (8 - (vals->hlen % 8)) % 8;
    }

    return vals;
}

/*
 * Read the intro part of a "signature" or "header" header.  These are
 * structurally the same, but contain different data.  Returns an
 * allocated struct rpmhdrintro on success (caller must free) or NULL
 * on error.
 */
struct rpmsignature *
read_header_signature(const int fd)
{
    struct rpmsignature *sig = NULL;

    if (fd <= 0) {
        return NULL;
    }

    /* zero out the structures */
    sig = xcalloc(1, sizeof(*sig));
    assert(sig != NULL);

    /* read in the signature */
    if (read(fd, sig, RPMHDRINTROSZ) != RPMHDRINTROSZ) {
        warn("read");
        free(sig);
        sig = NULL;
    }

    sig->magic = ntohl(sig->magic);
    sig->nentries = ntohl(sig->nentries);
    sig->nbytes = ntohl(sig->nbytes);

    /* verify the magic and reserved values are correct */
    if (!valid_header_signature(sig)) {
        free(sig);
        sig = NULL;
    }

    return sig;
}

/*
 * Given a header sig structure, read the entries block in to a
 * buffer for random access.  Returns an allocated buffer with the
 * data in it, or NULL on error.  The caller is responsible for
 * freeing the buffer.
 */
uint32_t *
read_header_entries(const int fd, const struct rpmsignature *sig, const uint32_t hlen)
{
    uint32_t *buffer = NULL;

    if (fd <= 0 || sig == NULL || hlen <= 0) {
        return NULL;
    }

    /* read in entries */
    /* (largely from rpmdump.c) */
    buffer = xcalloc(sig->nentries, sig->nbytes + hlen);
    assert(buffer != NULL);

    buffer[0] = htonl(sig->nentries);
    buffer[1] = htonl(sig->nbytes);

    if (read(fd, buffer + 2, hlen) != hlen) {
        warn("read");
        free(buffer);
        return NULL;
    }

    return buffer;
}

/*
 * Read and return the trailer if necessary.  Caller is responsible
 * for freeing the allocated trailer.
 */
struct rpmidxentry *
read_header_trailer(const struct rpmidxentry *entry, const uint8_t *datastart)
{
    struct rpmidxentry *trailer = NULL;
    rpmSigTag tag = 0;

    if (entry == NULL || datastart == NULL) {
        return NULL;
    }

    tag = ntohl(entry->tag);

    if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {
        trailer = xalloc(sizeof(*trailer));
        assert(trailer != NULL);
        memcpy(trailer, datastart + ntohl(entry->offset), sizeof(*trailer));
    }

    return trailer;
}
