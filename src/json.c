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

/* A 'tags' array entry with its tag number and where it sits */
struct tag_position {
    rpmTagVal tag;
    size_t position;
    struct json_object *entry;
};

/* Order by tag number and keep the array order for one tag number */
static int
compare_tag_position(const void *a, const void *b)
{
    const struct tag_position *x = a;
    const struct tag_position *y = b;

    if (x->tag != y->tag) {
        return (x->tag > y->tag) - (x->tag < y->tag);
    }

    return (x->position > y->position) - (x->position < y->position);
}

/*
 * Sort a 'tags' array by tag number.  qsort() is not stable, so we
 * break a tie on the position in the array, which is the header
 * order.  The copies of a tag that is there twice then go back in the
 * order they came out, and a round trip keeps the value rpm finds.
 * The signature flag reads the names as signature tags, since some
 * share a name with a header tag that has another number.
 */
void
sort_tags_stable(struct json_object *kvals, const bool signature)
{
    size_t i = 0;
    size_t n = 0;
    struct tag_position *items = NULL;

    if (kvals == NULL || json_object_get_type(kvals) != json_type_array) {
        return;
    }

    n = json_object_array_length(kvals);

    if (n < 2) {
        return;
    }

    items = xcalloc(n, sizeof(*items));

    /* we hold a reference to each so putting them back frees nothing */
    for (i = 0; i < n; i++) {
        items[i].entry = json_object_get(json_object_array_get_idx(kvals, i));
        items[i].tag = get_tag_number(items[i].entry, signature);
        items[i].position = i;
    }

    qsort(items, n, sizeof(*items), compare_tag_position);

    for (i = 0; i < n; i++) {
        json_object_array_put_idx(kvals, i, items[i].entry);
    }

    free(items);
    return;
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
generate_json(const struct rpmhdr *hdr)
{
    struct json_object *out = NULL;
    char *s = NULL;

    if (hdr == NULL) {
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
 * Return the locales in Headeri18ntable as a JSON array, or NULL when
 * the header has none.  read_header() has already checked the strings
 * fit in the header data.  The caller has to put the array.
 */
static struct json_object *
read_i18n_table(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t count = 0;
    char *p = NULL;
    struct json_object *table = NULL;

    for (i = 0; i < hdr->nentries; i++) {
        if (ntohl(hdrinfo->estart[i].tag) != HEADER_I18NTABLE || ntohl(hdrinfo->estart[i].type) != RPM_STRING_ARRAY_TYPE) {
            continue;
        }

        table = json_object_new_array();
        count = ntohl(hdrinfo->estart[i].count);
        p = (char *) hdrinfo->datastart + ntohl(hdrinfo->estart[i].offset);

        for (j = 0; j < count; j++) {
            json_object_array_add(table, json_object_new_string(p));
            p += strlen(p) + 1;
        }

        break;
    }

    return table;
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
    struct json_object *i18ntable = NULL;
    struct rpmhdrentry *hdrentry = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;
    i18ntable = read_i18n_table(hdr, hdrinfo);

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
        if (add_entry_value(entry, tag, hdrinfo->datastart, offset, datatype, count, dest_dir, signature) != 0) {
            json_object_put(entry);
            json_object_put(i18ntable);
            json_object_put(kvals);
            return NULL;
        }

        json_object_array_add(kvals, entry);

        if (datatype == RPM_I18NSTRING_TYPE && count > 1 && add_i18n_value(entry, tag, hdrinfo->datastart + offset, count, i18ntable, dest_dir) == -1) {
            json_object_put(i18ntable);
            json_object_put(kvals);
            return NULL;
        }
    }

    json_object_put(i18ntable);

    /* sort the array in ascending order by tag number */
    sort_tags_stable(kvals, signature);

    return kvals;
}

/*
 * Add the header tags that is_member picks to a 'tags' array.  We use
 * this when the dedicated array for a group of tags cannot be built,
 * so the values still reach header.json.  Returns true if the header
 * has any of those tags.
 */
bool
keep_raw_entries(struct json_object *kvals, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const char *dest_dir, bool (*is_member)(rpmTagVal))
{
    uint32_t i = 0;
    uint32_t tag = 0;
    bool found = false;
    struct json_object *entry = NULL;
    struct rpmhdrentry *hdrentry = NULL;

    if (kvals == NULL || hdr == NULL || hdrinfo == NULL || is_member == NULL) {
        return false;
    }

    hdrentry = hdrinfo->estart;

    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);

        if (!is_member(tag)) {
            continue;
        }

        entry = create_json_entry(&hdrentry[i], false);
        found = true;

        /* the caller fails the extract when we find any, so we only drop this one */
        if (add_entry_value(entry, tag, hdrinfo->datastart, ntohl(hdrentry[i].offset), ntohl(hdrentry[i].type), ntohl(hdrentry[i].count), dest_dir, false) != 0) {
            json_object_put(entry);
            continue;
        }

        json_object_array_add(kvals, entry);
    }

    if (found) {
        sort_tags_stable(kvals, false);
    }

    return found;
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
        warnx(_("*** unable to turn JSON object in to string"));
        return -1;
    }

    tostdout = !strcmp(path, OUTPUT_STDOUT);

    if (tostdout) {
        fp = stdout;
    } else {
        /* create output directory if needed */
        if (mkdirp_for_file(path) == -1) {
            warnx("mkdirp_for_file");
            return -1;
        }

        fp = fopen_output(path);

        if (fp == NULL) {
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

/* Return the length of the array key holds in obj, 0 if it is not one */
size_t
key_array_length(struct json_object *obj, const char *key)
{
    struct json_object *value = NULL;

    if (obj == NULL || key == NULL || !json_object_object_get_ex(obj, key, &value) || json_object_get_type(value) != json_type_array) {
        return 0;
    }

    return json_object_array_length(value);
}

/*
 * Print with -v how much header data holds.  The dependencies object
 * is counted across every kind of dependency.
 */
void
verbose_counts(struct json_object *header)
{
    size_t ntags = 0;
    size_t nfiles = 0;
    size_t ndeps = 0;
    size_t nchanges = 0;

    if (!verbose || header == NULL) {
        return;
    }

    ntags = key_array_length(header, RPM_ENTRY_TAGS_DESC);
    nfiles = key_array_length(header, RPM_FILES_DESC);
    ndeps = dependency_count(header);
    nchanges = key_array_length(header, RPM_CHANGELOG_DESC);

    verbose_detail("%zu %s, %zu %s, %zu %s, %zu %s", ntags, N_("tag", "tags", ntags), nfiles, N_("file", "files", nfiles), ndeps, N_("dependency", "dependencies", ndeps), nchanges, N_("changelog entry", "changelog entries", nchanges));
    return;
}
