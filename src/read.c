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
struct rpmhdrinfo *
mkhdrinfo(const struct rpmhdr *hdr, const bool signature)
{
    struct rpmhdrinfo *hdrinfo = NULL;

    if (hdr == NULL) {
        return NULL;
    }

    hdrinfo = xalloc(sizeof(*hdrinfo));

    /* computed from header values */
    /* NOTE: these values are already in host byte order */
    hdrinfo->ilen = hdr->nentries * sizeof(struct rpmhdrentry);
    hdrinfo->hlen = hdrinfo->ilen + hdr->nbytes;

    /* signature is aligned, so padding may be present */
    if (signature) {
        hdrinfo->padlen = (8 - (hdrinfo->hlen % 8)) % 8;
    } else {
        hdrinfo->padlen = 0;
    }

    return hdrinfo;
}

/*
 * Read the intro part of a "signature" or "header" header.  These are
 * structurally the same, but contain different data.  Returns an
 * allocated struct rpmhdrintro on success (caller must free) or NULL
 * on error.
 */
struct rpmhdr *
read_header_signature(const int fd)
{
    struct rpmhdr *hdr = NULL;

    if (fd < 0) {
        return NULL;
    }

    /* zero out the structures */
    hdr = xcalloc(1, sizeof(*hdr));

    /* read in the signature */
    if (!xread(fd, hdr, RPMHDRINTROSZ)) {
        free(hdr);
        return NULL;
    }

    /* tarpm does not do anything with the 'reserved' field, but the others are used */
    hdr->magic = ntohl(hdr->magic);
    hdr->nentries = ntohl(hdr->nentries);
    hdr->nbytes = ntohl(hdr->nbytes);

    /* verify the magic and reserved values are correct */
    if (!valid_header(hdr)) {
        free(hdr);
        return NULL;
    }

    return hdr;
}

/*
 * Given a header hdr structure, read the entries block in to a
 * buffer for random access.  Returns an allocated buffer with the
 * data in it, or NULL on error.  The caller is responsible for
 * freeing the buffer.
 */
uint32_t *
read_header_entries(const int fd, const struct rpmhdr *hdr, const uint32_t hlen)
{
    uint32_t *buffer = NULL;

    if (fd < 0 || hdr == NULL || hlen == 0) {
        return NULL;
    }

    /* read in entries */
    /* (largely from rpmdump.c) */
    buffer = xalloc(hlen + 2 * sizeof(uint32_t));

    buffer[0] = htonl(hdr->nentries);
    buffer[1] = htonl(hdr->nbytes);

    if (!xread(fd, buffer + 2, hlen)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

/*
 * Read and return the trailer if necessary.  Caller is responsible
 * for freeing the allocated trailer.
 */
struct rpmhdrentry *
read_header_trailer(const struct rpmhdr *hdr, const struct rpmhdrentry *estart, const uint8_t *datastart)
{
    uint32_t i = 0;
    struct rpmhdrentry *trailer = NULL;
    rpmSigTag tag = 0;
    int32_t offset = 0;

    if (hdr == NULL || estart == NULL || datastart == NULL) {
        return NULL;
    }

    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(estart[i].tag);
        offset = ntohl(estart[i].offset);

        if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {
            /* validate offset against buffer size */
            if ((size_t)offset + sizeof(struct rpmhdrentry) > hdr->nbytes) {
                warnx(_("trailer offset out of bounds"));
                return NULL;
            }

            trailer = xalloc(sizeof(*trailer));
            memcpy(trailer, datastart + offset, sizeof(*trailer));
            return trailer;
        }
    }

    return NULL;
}
