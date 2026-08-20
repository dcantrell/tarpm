/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    atag = get_tag_number(*aobj);
    btag = get_tag_number(*bobj);

    return (atag > btag) - (atag < btag);
}

/*
 * Turn a struct rpmhdrentry in to a json_object.
 */
struct json_object *
create_json_entry(const struct rpmhdrentry *hdrentry, const bool signature)
{
    struct json_object *entry = NULL;
    rpmSigTag tag = 0;
    rpmTagType datatype = 0;
    const char *tname = NULL;
    char *tagname = NULL;
    char *tagtype = NULL;

    if (hdrentry == NULL) {
        return NULL;
    }

    /* create a new object */
    entry = json_object_new_object();

    /* individual values for this object */
    tag = ntohl(hdrentry->tag);
    datatype = ntohl(hdrentry->type);

    /* add all of the entry values to the object */
    if (signature) {
        tname = sig_tag_name(tag);
    } else {
        tname = rpmTagGetName(tag);
    }

    if (!strcmp(tname, "(unknown)")) {
        xasprintf(&tagname, "#%d", tag);
    } else {
        xasprintf(&tagname, "%s", tname);
    }

    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(tagname));

    xasprintf(&tagtype, "%s", strtagtype(datatype));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(tagtype));

    /*
     * Mark cryptographic signature tags as read-only since they cannot be
     * recreated without the private signing keys. However, digest and size
     * tags that are recalculated by update_signature() should NOT be marked
     * as read-only.
     */
    if (signature) {
        /* These tags are recalculated by update_signature(), so they are NOT read-only */
        if (tag != RPMSIGTAG_SIZE && tag != RPMSIGTAG_LONGSIZE && tag != RPMSIGTAG_PAYLOADSIZE && tag != RPMSIGTAG_MD5 && tag != RPMSIGTAG_SHA1 && tag != RPMSIGTAG_SHA256) {
            /* All other signature tags are read-only (RSA, DSA, etc.) */
            json_object_object_add(entry, RPM_METADATA_READ_ONLY, json_object_new_string("true"));
        }
    }

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

    return out;
}

/*
 * Generate a "signature" or "header" JSON array of entries for the
 * tags for output.
 */
struct json_object *
generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const char *dest_dir, const bool signature)
{
    uint32_t i = 0;
    uint32_t tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    struct json_object *kvals = NULL;
    struct json_object *entry = NULL;
    struct json_object *jtrailer = NULL;
    struct rpmhdrentry *hdrentry = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;

    /* create a new array for these tags */
    kvals = json_object_new_array();

    /* add each tag to the array */
    for (i = 0; i < hdr->nentries; i++) {
        /* the header entries are still in network byte order from the file */
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);

        /* skip tags that go to dedicated arrays */
        if (is_changelog_tag(tag) || is_dependency_tag(tag)) {
            continue;
        }

        entry = create_json_entry(&hdrentry[i], signature);

        /*
         * header tags of these types will have a trailer that we need
         * to capture and compute
         */
        if (trailer != NULL && (tag == HEADER_SIGNATURES || tag == HEADER_IMMUTABLE)) {
            /* create a new array just for the trailer */
            jtrailer = create_json_entry(trailer, signature);

            /* add the trailer to this entry because of the tag type */
            json_object_object_add(entry, RPM_ENTRY_TRAILER_DESC, jtrailer);
        }

        /* add the entry to the array */
        add_entry_value(entry, tag, hdrinfo->datastart, offset, datatype, count, dest_dir);
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

    if (input_file == NULL) {
        return NULL;
    }

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
    int q = 0;
    int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;

    if (data == NULL || output_dir == NULL || output_file == NULL) {
        return -1;
    }

    /* write the JSON data for a file */
    s = joinpath(output_dir, output_file, NULL);

    if (s == NULL) {
        warn("joinpath");
        return -1;
    }

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

    if (fprintf(fp, "%s\n", js) < 0) {
        warn("fprintf");
        fclose(fp);
        return -1;
    }

    r = fflush(fp);

    if (r != 0) {
        warn("fflush");
    }

    q = fclose(fp);

    if (q != 0) {
        warn("fclose");
    }

    if (r || q) {
        return -1;
    }

    return 0;
}
