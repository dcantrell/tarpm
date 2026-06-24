/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <json.h>
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
 * Turn a struct rpmhdrentry in to a json_object.
 */
struct json_object *
create_json_hdr_entry(const struct rpmhdrentry *hdrentry, const bool signature)
{
    struct json_object *entry = NULL;
    rpmSigTag tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    char *tagname = NULL;
    char *tagtype = NULL;

    if (hdrentry == NULL) {
        return NULL;
    }

    /* create a new object */
    entry = json_object_new_object();
    assert(entry != NULL);

    /* individual values for this object */
    tag = ntohl(hdrentry->tag);
    offset = ntohl(hdrentry->offset);
    datatype = ntohl(hdrentry->type);
    count = ntohl(hdrentry->count);

    /* add all of the entry values to the object */
    if (signature) {
        xasprintf(&tagname, "%s", sig_tag_name(tag));
    } else {
        xasprintf(&tagname, "%s", rpmTagGetName(tag));
    }

    json_object_object_add(entry, RPM_ENTRY_NAME_DESC, json_object_new_string(tagname));

    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_int64(tag));

    xasprintf(&tagtype, "%s", strtagtype(datatype));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(tagtype));

    json_object_object_add(entry, RPM_ENTRY_OFFSET_DESC, json_object_new_int64(offset));

    json_object_object_add(entry, RPM_ENTRY_COUNT_DESC, json_object_new_int64(count));

    /* clean up */
    free(tagname);
    free(tagtype);

    return entry;
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

    xasprintf(&s, "%04d", hdr->reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%u", hdr->nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%" PRIu32, hdrinfo->ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%" PRIu32, hdr->nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%" PRIu32, hdrinfo->hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    return out;
}

/*
 * Generate a "signature" or "header" JSON array of entries for the
 * tags for output.
 */
struct json_object *
generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const bool signature)
{
    uint32_t i = 0;
    rpmSigTag tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    struct json_object *kvals = NULL;
    struct json_object *entry = NULL;
    struct json_object *jtrailer = NULL;
    struct rpmhdrentry *hdrentry = hdrinfo->estart;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    /* create a new array for these tags */
    kvals = json_object_new_array();
    assert(kvals != NULL);

    /* add each tag to the array */
    for (i = 0; i < hdr->nentries; i++) {
        /* the header entries are still in network byte order from the file */
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);

        entry = create_json_hdr_entry(&hdrentry[i], signature);
        assert(entry != NULL);

        /*
         * header tags of these types will have a trailer that we need
         * to capture and compute
         */
        if (trailer != NULL && (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE)) {
            /* create a new array just for the trailer */
            jtrailer = create_json_hdr_entry(trailer, signature);
            assert(jtrailer != NULL);

            /* add the trailer to this entry because of the tag type */
            json_object_object_add(entry, RPM_ENTRY_TRAILER_DESC, jtrailer);
        }

        /* add the entry to the array */
        add_entry_value(entry, hdrinfo->datastart, offset, datatype, count);
        json_object_array_add(kvals, entry);
    }

    /* sort the array in ascending order by tag number */
    json_object_array_sort(kvals, sort_by_tag_number);

    return kvals;
}

/*
 * Wrapper for reading in a JSON file.
 */
struct json_object *
read_json_file(const char *input_file)
{
    struct json_object *obj = NULL;

    assert(input_file != NULL);

    if (access(input_file, R_OK) == -1) {
        warn(_("*** missing or unreadable %s"), input_file);
        return NULL;
    }

    obj = json_object_from_file(input_file);

    if (obj == NULL) {
        warnx(_("*** json_object_from_file: %s"), json_util_get_last_err());
        return NULL;
    }

    return obj;
}

/*
 * Takes the JSON data and writes it to the output_file in output_dir.
 * Returns 0 on success, -1 on error.
 */
int
write_json_file(struct json_object *data, const char *output_dir, const char *output_file)
{
    char *s = NULL;
    const char *js = NULL;
    FILE *fp = NULL;
    int r = 0;
    int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;

    if (data == NULL || output_dir == NULL || output_file == NULL) {
        return -1;
    }

    /* write the JSON data for a file */
    s = joinpath(output_dir, output_file, NULL);
    assert(s != NULL);

    fp = fopen(s, "w");

    if (fp == NULL) {
        warn("fopen");
        free(s);
        return -1;
    }

    free(s);
    js = json_object_to_json_string_ext(data, flags);

    if (js == NULL) {
        errx(EXIT_FAILURE, "unable to turn JSON object in to string");
    }

    fprintf(fp, "%s\n", js);
    r += fflush(fp);

    if (r != 0) {
        warn("fflush");
    }

    r += fclose(fp);

    if (r != 0) {
        warn("fclose");
    }

    return r;
}
