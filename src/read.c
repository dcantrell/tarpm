/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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

    assert(sig != NULL);

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

    assert(fd > 0);

    /* zero out the structures */
    sig = xcalloc(1, sizeof(*sig));
    assert(sig != NULL);

    /* read in the signature */
    if (read(fd, sig, RPMHDRINTROSZ) != RPMHDRINTROSZ) {
        warn("read");
        goto bad;
    }

    sig->magic = ntohl(sig->magic);
    sig->nentries = ntohl(sig->nentries);
    sig->nbytes = ntohl(sig->nbytes);

    /* verify the magic and reserved values are correct */
    if (sig->magic != RPM_SIGNATURE_MAGIC) {
        warn("magic value mismatch, not an RPM");
        goto bad;
    }

    if (sig->reserved != 0) {
        warn("reserved value mismatch, not an RPM");
        goto bad;
    }

    return sig;

bad:
    free(sig);
    return NULL;
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

    assert(fd > 0);
    assert(sig != NULL);
    assert(hlen > 0);

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

    assert(entry != NULL);
    assert(datastart != NULL);

    tag = ntohl(entry->tag);

    if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {
        trailer = xalloc(sizeof(*trailer));
        assert(trailer != NULL);
        memcpy(trailer, datastart + ntohl(entry->offset), sizeof(*trailer));
    }

    return trailer;
}



























//
//
//    uint32_t i = 0;
//    uint32_t offset = 0;
//    rpmTagType datatype = 0;
//    uint32_t count = 0;
//    uint32_t padlen = 0;
//    struct rpmsighdr pad;
//    struct json_object *out = NULL;
//    struct json_object *vals = NULL;
//    struct json_object *arrayentry = NULL;
//    char *s = NULL;
//
//    assert(output_dir != NULL);
//
//
//
//
//
//
//    /* signature is aligned, so padding may be present */
//    padlen = (8 - (hlen % 8)) % 8;
//
//    if (read(fd, &pad, padlen) != padlen) {
//        err(EXIT_FAILURE, "read");
//    }
//
//    /* generate a JSON structure for the signature */
//    out = json_object_new_object();
//
//    xasprintf(&s, "0x%X", sig.magic);
//    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
//    free(s);
//
//    xasprintf(&s, "%04d", sig.reserved);
//    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
//    free(s);
//
//    xasprintf(&s, "%d", sig.nentries);
//    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
//    free(s);
//
//    xasprintf(&s, "%d", ilen);
//    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
//    free(s);
//
//    xasprintf(&s, "%d", sig.nbytes);
//    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
//    free(s);
//
//    xasprintf(&s, "%d", hlen);
//    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
//    free(s);
//
//    /* dump all of the tags in the signature */
//    vals = json_object_new_array();
//
//    /* handle trailer */
//    /* the trailer is not guaranteed to be aligned, copy required */
//    /* from rpmdump.c in rpm's source */
//    entry = (struct rpmhdrentry *) (buffer + 2);
//    tag = ntohl(entry->tag);
//    datastart = (uint8_t *) (estart + sig.nentries);
//
//    if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {
//        trailer = xalloc(sizeof(*trailer));
//        assert(trailer != NULL);
//        memcpy(trailer, datastart + ntohl(entry->offset), sizeof(*trailer));
//    }
//
//    for (i = 0; i < sig.nentries; i++) {
//        /* create the JSON data */
//        tag = ntohl(entry[i].tag);
//        offset = ntohl(entry[i].offset);
//        datatype = ntohl(entry[i].type);
//        count = ntohl(entry[i].count);
//        arrayentry = json_object_new_object();
//
//        xasprintf(&s, "%s", signature_tag_name(tag));
//        json_object_object_add(arrayentry, RPM_SIGNATURE_NAME_DESC, json_object_new_string(s));
//        free(s);
//
//        xasprintf(&s, "%d", tag);
//        json_object_object_add(arrayentry, RPM_SIGNATURE_TAG_DESC, json_object_new_string(s));
//        free(s);
//
//        xasprintf(&s, "%s", tag_type(datatype));
//        json_object_object_add(arrayentry, RPM_SIGNATURE_TYPE_DESC, json_object_new_string(s));
//        free(s);
//
//        xasprintf(&s, "0x%X", offset);
//        json_object_object_add(arrayentry, RPM_SIGNATURE_OFFSET_DESC, json_object_new_string(s));
//        free(s);
//
//        xasprintf(&s, "%d", count);
//        json_object_object_add(arrayentry, RPM_SIGNATURE_COUNT_DESC, json_object_new_string(s));
//        free(s);
//
//        add_entry_value(arrayentry, datastart, offset, datatype, count);
//        json_object_array_add(vals, arrayentry);
//    }
//
//    json_object_object_add(out, RPM_SIGNATURE_TAGS_DESC, json_object_get(vals));
//
//    /* write the signature to a file */
//    if (write_json_file(out, output_dir, OUTPUT_SIGNATURE) != 0) {
//        warn("write_json_file");
//    }
//
//    /* cleanup */
//    free_json(out);
//
//    return 0;
//}
//
