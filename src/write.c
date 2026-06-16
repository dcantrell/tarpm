/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <assert.h>
#include <arpa/inet.h>
#include "tarpm.h"

/*
 * Used by generate_json_entries() below to sort the 'tags' array.
 */
static int
sort_by_tag_number(const void *a, const void *b)
{
    struct json_object **aobj = (struct json_object **) a;
    struct json_object **bobj = (struct json_object **) b;
    int atag = 0;
    int btag = 0;
    struct json_object *obj = NULL;

    /* handle special conditions */
    if (*aobj == NULL && *bobj == NULL) {
        return 0;
    }

    if (*aobj == NULL) {
        return -1;
    }

    if (*bobj == NULL) {
        return 1;
    }

    /* get the tag numbers for sorting */
    obj = json_object_object_get(*aobj, RPM_ENTRY_TAG_DESC);
    atag = json_object_get_int(obj);

    obj = json_object_object_get(*bobj, RPM_ENTRY_TAG_DESC);
    btag = json_object_get_int(obj);

    return atag - btag;
}

/*
 * Generate a "signature" or "header" JSON structure for output.
 */
struct json_object *
generate_json(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo)
{
    struct json_object *out = NULL;
    char *s = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    out = json_object_new_object();

    xasprintf(&s, "0x%X", hdr->magic);
    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "0x%X", hdr->reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%u", hdr->nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hdrinfo->ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hdr->nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", hdrinfo->hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    return out;
}

/*
 * Generate a "signature" or "header" JSON array of entries for the
 * tags for output.
 */
struct json_object *
generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *entry, const bool signature)
{
    uint32_t i = 0;
    rpmSigTag tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    struct json_object *jvals = NULL;
    struct json_object *arrayentry = NULL;
    char *s = NULL;

    if (hdr == NULL || hdrinfo == NULL || entry == NULL) {
        return NULL;
    }

    /* create a new array for these tags */
    jvals = json_object_new_array();

    /* add each tag to the array */
    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(entry[i].tag);
        offset = ntohl(entry[i].offset);
        datatype = ntohl(entry[i].type);
        count = ntohl(entry[i].count);
        arrayentry = json_object_new_object();

        if (signature) {
            xasprintf(&s, "%s", signature_tag_name(tag));
        } else {
            xasprintf(&s, "%s", rpmTagGetName(tag));
        }

        json_object_object_add(arrayentry, RPM_ENTRY_NAME_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", tag);
        json_object_object_add(arrayentry, RPM_ENTRY_TAG_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%s", strtagtype(datatype));
        json_object_object_add(arrayentry, RPM_ENTRY_TYPE_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "0x%X", offset);
        json_object_object_add(arrayentry, RPM_ENTRY_OFFSET_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", count);
        json_object_object_add(arrayentry, RPM_ENTRY_COUNT_DESC, json_object_new_string(s));
        free(s);

        /*
         * header tags of these types will have a trailer that we need
         * to capture and compute
         */
        if (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE) {

        }

        add_entry_value(arrayentry, hdrinfo->datastart, offset, datatype, count);
        json_object_array_add(jvals, arrayentry);
    }

    /* sort the array in ascending order by tag number */
    json_object_array_sort(jvals, sort_by_tag_number);

    return jvals;
}
