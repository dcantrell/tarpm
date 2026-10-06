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
 * Compare two entries of a 'tags' array by tag number.  The signature
 * flag tells us to read the names as signature tags, which we have to
 * do because some signature tags share a name with a header tag that
 * carries a different number.
 */
static int
compare_tag_number(const void *a, const void *b, const bool signature)
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
    atag = get_tag_number(*aobj, signature);
    btag = get_tag_number(*bobj, signature);

    return (atag > btag) - (atag < btag);
}

/*
 * Used by generate_json_entries() below to sort a header 'tags' array.
 */
static int
sort_by_tag_number(const void *a, const void *b)
{
    return compare_tag_number(a, b, false);
}

/*
 * Used by generate_json_entries() below to sort a signature 'tags'
 * array.
 */
static int
sort_by_sig_tag_number(const void *a, const void *b)
{
    return compare_tag_number(a, b, true);
}

/*
 * Turn a struct rpmhdrentry in to a json_object.
 */
struct json_object *
create_json_entry(const struct rpmhdrentry *hdrentry, const bool signature)
{
    struct json_object *entry = NULL;
    rpmSigTag tag = 0;
    rpmTagVal htag = 0;
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

    /*
     * The digest algorithms are recorded by name and the build time as
     * a timestamp in header.json, so those are strings there even
     * though all of them are int32 in the header itself.
     */
    htag = (rpmTagVal) tag;

    if (!signature && (htag == RPMTAG_FILEDIGESTALGO || htag == RPMTAG_PAYLOAD_DIGEST_ALGO || htag == RPMTAG_BUILDTIME) && datatype == RPM_INT32_TYPE) {
        datatype = RPM_STRING_TYPE;
    }

    xasprintf(&tagtype, "%s", strtagtype(datatype));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(tagtype));

    /*
     * Mark signature header tags are read-only to reiterate to the
     * user that this information will be recalculated when you create
     * an RPM.
     */
    if (signature) {
        json_object_object_add(entry, RPM_METADATA_READ_ONLY, json_object_new_string("true"));
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
        if (is_changelog_tag(tag) || is_dependency_tag(tag) || is_file_list_tag(tag)) {
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
        add_entry_value(entry, tag, hdrinfo->datastart, offset, datatype, count, dest_dir, signature);
        json_object_array_add(kvals, entry);
    }

    /* sort the array in ascending order by tag number */
    if (signature) {
        json_object_array_sort(kvals, sort_by_sig_tag_number);
    } else {
        json_object_array_sort(kvals, sort_by_tag_number);
    }

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
 * Takes the JSON data and writes it to the file named by path.  A
 * path of a single hyphen sends the JSON to stdout, which the caller
 * keeps.  Returns 0 on success, -1 on error.
 */
int
write_json_file(struct json_object *data, const char *path)
{
    const char *js = NULL;
    int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;
    int r = 0;
    int q = 0;
    bool tostdout = false;
    FILE *fp = NULL;

    if (data == NULL || path == NULL) {
        return -1;
    }

    js = json_object_to_json_string_ext(data, flags);

    if (js == NULL) {
        errx(EXIT_FAILURE, "unable to turn JSON object in to string");
    }

    tostdout = !strcmp(path, OUTPUT_STDOUT);

    if (tostdout) {
        fp = stdout;
    } else {
        fp = fopen(path, "w");

        if (fp == NULL) {
            warn("fopen");
            return -1;
        }
    }

    if (fprintf(fp, "%s\n", js) < 0) {
        warn("fprintf");
        r = -1;
    }

    if (fflush(fp) != 0) {
        warn("fflush");
        r = -1;
    }

    /* stdout is not ours to close */
    if (!tostdout) {
        q = fclose(fp);

        if (q != 0) {
            warn("fclose");
        }
    }

    if (r || q) {
        return -1;
    }

    return 0;
}
