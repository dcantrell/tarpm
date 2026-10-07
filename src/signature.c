/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <err.h>
#include <string.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmbase64.h>
#include <openssl/md5.h>

#include "tarpm.h"

/* the default signature header tags, one group per RPM format */
static const uint32_t v4_signature_tags[] = { RPMFORMAT_V4_SIGNATURE_TAGS };
static const uint32_t v6_signature_tags[] = { RPMFORMAT_V6_SIGNATURE_TAGS };

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
    hdrinfo = mkhdrinfo(rawhdr, true);

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
    trailer = read_header_trailer(rawhdr, hdrinfo->estart, hdrinfo->datastart);

    /* generate a JSON structure for the signature */
    signature = generate_json(rawhdr, hdrinfo);

    /* dump all of the tags in the signature */
    jvals = generate_json_entries(rawhdr, hdrinfo, trailer, NULL, true);

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

/*
 * Build the JSON entry for one signature header tag.  The value is a
 * placeholder of the right type, update_signature() in create.c puts
 * the real digests and sizes in once the header and the payload
 * exist.  Returns an allocated json_object, NULL on error.
 */
static struct json_object *
make_signature_tag(const uint32_t tag)
{
    struct json_object *entry = NULL;
    struct json_object *trailer = NULL;
    struct rpmhdrentry region;
    rpmTagType type = RPM_BIN_TYPE;
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    char *value = NULL;

    switch (tag) {
        case HEADER_SIGNATURES:
            /*
             * The region trailer, which is an index entry pointing
             * back at the start of the index.  write_header() works
             * the offset out again when it knows how many entries we
             * wrote.
             */
            region.tag = htonl(HEADER_SIGNATURES);
            region.type = htonl(RPM_BIN_TYPE);
            region.offset = 0;
            region.count = htonl(sizeof(region));
            blobsize = sizeof(region);
            blob = xalloc(blobsize);
            memcpy(blob, &region, blobsize);
            break;
        case RPMSIGTAG_MD5:
            blobsize = MD5_DIGEST_LENGTH;
            blob = xcalloc(blobsize, sizeof(*blob));
            break;
        case RPMSIGTAG_RESERVEDSPACE:
        case RPMSIGTAG_RESERVED_VALUE:
            blobsize = RPM_SIGNATURE_RESERVED_SIZE;
            blob = xcalloc(blobsize, sizeof(*blob));
            break;
        case RPMSIGTAG_SIZE:
        case RPMSIGTAG_PAYLOADSIZE:
            type = RPM_INT32_TYPE;
            break;
        default:
            /* the digests are recorded as hex strings */
            type = RPM_STRING_TYPE;
            break;
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(sig_tag_name(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));

    /* the signature header is provided for informational purposes only */
    json_object_object_add(entry, RPM_METADATA_READ_ONLY, json_object_new_string("true"));

    /* the region tag carries the trailer the writer needs */
    if (tag == HEADER_SIGNATURES) {
        trailer = json_object_new_object();
        json_object_object_add(trailer, RPM_ENTRY_TAG_DESC, json_object_new_string(sig_tag_name(tag)));
        json_object_object_add(trailer, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_BIN_TYPE)));
        json_object_object_add(entry, RPM_ENTRY_TRAILER_DESC, trailer);
    }

    if (type == RPM_BIN_TYPE) {
        value = rpmBase64Encode(blob, blobsize, -1);
        free(blob);

        if (value == NULL) {
            warnx("rpmBase64Encode");
            json_object_put(entry);
            return NULL;
        }

        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
        free(value);
    } else if (type == RPM_INT32_TYPE) {
        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_int(0));
    } else {
        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(""));
    }

    return entry;
}

/*
 * Build the signature header rpm would write for the given format.
 * We make this ourselves rather than read signature.json because
 * every tag in it is recalculated anyway.  Returns an allocated
 * json_object (caller must free), NULL on error.
 */
struct json_object *
make_signature(const int format)
{
    size_t i = 0;
    size_t ntags = 0;
    const uint32_t *tags = NULL;
    struct json_object *signature = NULL;
    struct json_object *entries = NULL;
    struct json_object *entry = NULL;
    char *s = NULL;

    if (format == RPM_FORMAT_V4) {
        tags = v4_signature_tags;
        ntags = sizeof(v4_signature_tags) / sizeof(v4_signature_tags[0]);
    } else if (format == RPM_FORMAT_V6) {
        tags = v6_signature_tags;
        ntags = sizeof(v6_signature_tags) / sizeof(v6_signature_tags[0]);
    } else {
        warnx(_("*** unsupported RPM format: %d"), format);
        return NULL;
    }

    signature = json_object_new_object();

    xasprintf(&s, "0x%X", RPM_SIGNATURE_MAGIC);
    json_object_object_add(signature, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%04d", RPM_SIGNATURE_RESERVED);
    json_object_object_add(signature, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    entries = json_object_new_array();

    for (i = 0; i < ntags; i++) {
        entry = make_signature_tag(tags[i]);

        if (entry == NULL) {
            json_object_put(entries);
            json_object_put(signature);
            return NULL;
        }

        json_object_array_add(entries, entry);
    }

    json_object_object_add(signature, RPM_ENTRY_TAGS_DESC, entries);

    return signature;
}
