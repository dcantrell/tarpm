/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>
#include <rpm/rpmtd.h>
#include <json.h>
#include <time.h>

#include "tarpm.h"

static rpmTagType
get_entry_type(struct json_object *entry)
{
    struct json_object *key = NULL;

    if (entry == NULL) {
        return RPM_NULL_TYPE;
    }

    if (json_object_object_get_ex(entry, RPM_ENTRY_TYPE_DESC, &key)) {
        return tag_type(key);
    }

    return RPM_NULL_TYPE;
}

static size_t
get_item_size(size_t index, struct json_object *entry, int32_t *trailer_index, size_t *trailer_size)
{
    size_t item_size = 0;
    rpmTagType entry_type = RPM_NULL_TYPE;
    rpmTagVal tag_number = 0;
    int r = 0;
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    const char *value = NULL;
    struct json_object *key = NULL;
    size_t j = 0;
    size_t len = 0;
    struct json_object *s = NULL;
    const char *filepath = NULL;
    off_t filelen = 0;
    char *filedata = NULL;

    if (entry == NULL) {
        return 0;
    }

    entry_type = get_entry_type(entry);
    tag_number = get_tag_number(entry);

    /* get the value field from the entry */
    if (is_file_tag(tag_number)) {
        if (!json_object_object_get_ex(entry, RPM_ENTRY_FILE_DESC, &key)) {
            return 0;
        }
    } else {
        if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &key)) {
            return 0;
        }
    }

    if (entry_type == RPM_BIN_TYPE) {
        /* binary data: base64 decode to get actual size */
        value = json_object_get_string(key);
        r = rpmBase64Decode(value, (void **) &blob, &blobsize);

        if (r == 0) {
            /* in the trailer - track it separately */
            if (tag_number == HEADER_SIGNATURES || tag_number == HEADER_IMMUTABLE) {
                *trailer_index = index;
                *trailer_size = blobsize;
            } else {
                item_size = blobsize;
            }

            free(blob);
            blob = NULL;
        }
    } else if (entry_type == RPM_INT8_TYPE) {
        if (json_object_get_type(key) == json_type_array) {
            item_size = sizeof(uint8_t) * json_object_array_length(key);
        } else {
            item_size = sizeof(uint8_t);
        }
    } else if (entry_type == RPM_INT16_TYPE) {
        if (json_object_get_type(key) == json_type_array) {
            item_size = sizeof(uint16_t) * json_object_array_length(key);
        } else {
            item_size = sizeof(uint16_t);
        }
    } else if (entry_type == RPM_INT32_TYPE) {
        if (json_object_get_type(key) == json_type_array) {
            item_size = sizeof(uint32_t) * json_object_array_length(key);
        } else {
            item_size = sizeof(uint32_t);
        }
    } else if (entry_type == RPM_INT64_TYPE) {
        if (json_object_get_type(key) == json_type_array) {
            item_size = sizeof(uint64_t) * json_object_array_length(key);
        } else {
            item_size = sizeof(uint64_t);
        }
    } else if (entry_type == RPM_STRING_ARRAY_TYPE) {
        /* string array: sum of all string lengths + NULs */
        j = 0;
        len = json_object_array_length(key);
        s = NULL;

        for (j = 0; j < len; j++) {
            s = json_object_array_get_idx(key, j);
            item_size += strlen(json_object_get_string(s)) + 1;
        }
    } else {
        /* string data: length + NUL */
        if (is_file_tag(tag_number)) {
            /* for file tags, we need to get the actual file size */
            filepath = json_object_get_string(key);
            filelen = 0;
            filedata = read_file_bytes(filepath, &filelen);

            if (filedata != NULL) {
                item_size = filelen + 1;
                free(filedata);
            } else {
                item_size = json_object_get_string_len(key) + 1;
            }
        } else {
            item_size = json_object_get_string_len(key) + 1;
        }
    }

    return item_size;
}

/* Calculate the size of the data buffer for this header. */
static size_t
get_data_buffer_size(struct json_object *tags, int32_t *trailer_index, size_t *trailer_size)
{
    size_t datasize = 0;
    size_t i = 0;
    const char *field = NULL;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    rpmTagVal tag_number = 0;
    rpmTagType entry_type = RPM_NULL_TYPE;
    int32_t padding = 0;
    size_t item_size = 0;

    if (tags == NULL) {
        return 0;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        /* get the tag in the array */
        entry = json_object_array_get_idx(tags, i);

        /* get the tag number and type from this entry */
        tag_number = get_tag_number(entry);
        entry_type = get_entry_type(entry);

        /* get the field name based on the tag number */
        if (is_file_tag(tag_number)) {
            field = RPM_ENTRY_FILE_DESC;
        } else {
            field = RPM_ENTRY_VALUE_DESC;
        }

        /* get the value and calculate size */
        if (json_object_object_get_ex(entry, field, &key)) {
            item_size = get_item_size(i, entry, trailer_index, trailer_size);

            /* sequential calculation with alignment */
            if (entry_type == RPM_INT16_TYPE) {
                padding = (2 - (datasize % 2)) % 2;
            } else if (entry_type == RPM_INT32_TYPE) {
                padding = (4 - (datasize % 4)) % 4;
            } else if (entry_type == RPM_INT64_TYPE) {
                padding = (8 - (datasize % 8)) % 8;
            } else {
                padding = 0;
            }

            datasize += padding + item_size;
        }
    }

    return datasize;
}

/* Add the header tags and their values to the data buffer */
static int
add_header_tags(struct json_object *tags, struct rpmhdrinfo *v, size_t totalsize, int32_t trailer_index, size_t trailer_size)
{
    int r = 0;
    int b = 0;
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    uint8_t *datapos = NULL;
    int32_t offset = 0;
    uint8_t i8 = 0;
    uint16_t i16 = 0;
    uint32_t i32 = 0;
    uint64_t i64 = 0;
    char *tmp = NULL;
    const char *value = NULL;
    const char *field = NULL;
    int len = 0;
    uint8_t *blob = NULL;
    int32_t padding = 0;
    size_t j = 0;
    struct json_object *obj = NULL;

    if (tags == NULL || v == NULL) {
        return -1;
    }

    /* position the data buffer and offset */
    datapos = v->datastart;
    offset = 0;

    /* create header tags and copy in the values */
    for (i = 0; i < json_object_array_length(tags); i++) {
        padding = 0;
        entry = json_object_array_get_idx(tags, i);
        v->entry->tag = get_tag_number(entry);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TYPE_DESC, &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'type'"));
            r = -1;
        } else {
            v->entry->type = tag_type(key);
        }

        /* calculate offset */
        if (trailer_index >= 0 && i == ((size_t) trailer_index)) {
            /* trailer offset points to end of data (past actual data) */
            /* trailer is not written to data buffer when creating */
            v->entry->offset = totalsize;
        } else {
            /* compute offset and write data sequentially */
            /* add alignment padding for integer types (4-byte alignment) */
            if (v->entry->type == RPM_INT16_TYPE) {
                padding = (2 - (offset % 2)) % 2;
            } else if (v->entry->type == RPM_INT32_TYPE) {
                padding = (4 - (offset % 4)) % 4;
            } else if (v->entry->type == RPM_INT64_TYPE) {
                padding = (8 - (offset % 8)) % 8;
            }

            offset += padding;
            datapos += padding;
            v->entry->offset = offset;
        }

        /* write data for all entries except trailer */
        if (!(trailer_index >= 0 && i == ((size_t) trailer_index))) {
            /* get the field name based on the tag type */
            if (is_file_tag(v->entry->tag)) {
                field = RPM_ENTRY_FILE_DESC;
            } else {
                field = RPM_ENTRY_VALUE_DESC;
            }

            /* now get the data and put it in the buffer and update the offset */
            if (json_object_object_get_ex(entry, field, &key) == 0) {
                warnx(_("*** invalid header tag entry, missing '%s'"), field);
                r = -1;
            } else {
                /* handle each data type */
                if (v->entry->type == RPM_BIN_TYPE) {
                    value = json_object_get_string(key);
                    b = rpmBase64Decode(value, (void **) &blob, (size_t *) &(v->entry->count));

                    if (b == 0) {
                        memcpy(datapos, blob, v->entry->count);
                        datapos += v->entry->count;
                        offset += v->entry->count;
                        free(blob);
                    } else {
                        warnx(_("*** rpmBase64Decode failed with code %d"), b);
                        r = -1;
                    }
                } else if (v->entry->type == RPM_INT8_TYPE) {
                    if (json_object_get_type(key) == json_type_array) {
                        j = 0;
                        v->entry->count = json_object_array_length(key);
                        obj = NULL;

                        for (j = 0; j < v->entry->count; j++) {
                            obj = json_object_array_get_idx(key, j);
                            i8 = json_object_get_int(obj);
                            memcpy(datapos, &i8, sizeof(i8));
                            datapos += sizeof(i8);
                            offset += sizeof(i8);
                        }
                    } else {
                        v->entry->count = 1;
                        i8 = json_object_get_int(key);
                        memcpy(datapos, &i8, sizeof(i8));
                        datapos += sizeof(i8);
                        offset += sizeof(i8);
                    }
                } else if (v->entry->type == RPM_INT16_TYPE) {
                    if (json_object_get_type(key) == json_type_array) {
                        j = 0;
                        v->entry->count = json_object_array_length(key);
                        obj = NULL;

                        for (j = 0; j < v->entry->count; j++) {
                            obj = json_object_array_get_idx(key, j);
                            i16 = htons(json_object_get_uint64(obj));
                            memcpy(datapos, &i16, sizeof(i16));
                            datapos += sizeof(i16);
                            offset += sizeof(i16);
                        }
                    } else {
                        v->entry->count = 1;
                        i16 = htons(json_object_get_uint64(key));
                        memcpy(datapos, &i16, sizeof(i16));
                        datapos += sizeof(i16);
                        offset += sizeof(i16);
                    }
                } else if (v->entry->type == RPM_INT32_TYPE) {
                    if (json_object_get_type(key) == json_type_array) {
                        j = 0;
                        v->entry->count = json_object_array_length(key);
                        obj = NULL;

                        for (j = 0; j < v->entry->count; j++) {
                            obj = json_object_array_get_idx(key, j);
                            i32 = htonl((uint32_t) json_object_get_int64(obj));
                            memcpy(datapos, &i32, sizeof(i32));
                            datapos += sizeof(i32);
                            offset += sizeof(i32);
                        }
                    } else {
                        v->entry->count = 1;
                        i32 = htonl((uint32_t) json_object_get_int64(key));
                        memcpy(datapos, &i32, sizeof(i32));
                        datapos += sizeof(i32);
                        offset += sizeof(i32);
                    }
                } else if (v->entry->type == RPM_INT64_TYPE) {
                    if (json_object_get_type(key) == json_type_array) {
                        j = 0;
                        v->entry->count = json_object_array_length(key);
                        obj = NULL;

                        for (j = 0; j < v->entry->count; j++) {
                            obj = json_object_array_get_idx(key, j);
                            i64 = htobe64((uint64_t) json_object_get_int64(obj));
                            memcpy(datapos, &i64, sizeof(i64));
                            datapos += sizeof(i64);
                            offset += sizeof(i64);
                        }
                    } else {
                        v->entry->count = 1;
                        i64 = htobe64((uint64_t) json_object_get_int64(key));
                        memcpy(datapos, &i64, sizeof(i64));
                        datapos += sizeof(i64);
                        offset += sizeof(i64);
                    }
                } else if (v->entry->type == RPM_STRING_ARRAY_TYPE) {
                    /* string array: write each string with NUL terminator */
                    j = 0;
                    obj = NULL;
                    v->entry->count = json_object_array_length(key);

                    for (j = 0; j < v->entry->count; j++) {
                        obj = json_object_array_get_idx(key, j);
                        value = json_object_get_string(obj);
                        len = strlen(value);
                        memcpy(datapos, value, len + 1);
                        datapos += len + 1;
                        offset += len + 1;
                    }
                } else {
                    /* string data */
                    v->entry->count = 1;
                    value = json_object_get_string(key);

                    if (is_file_tag(v->entry->tag)) {
                        /* read in this tag's value from the named file */
                        tmp = read_file(value);

                        if (tmp == NULL) {
                            warnx(_("*** empty or non-existent file: %s"), value);
                            len = 0;
                        } else {
                            len = strlen(tmp);
                            memcpy(datapos, tmp, len + 1);
                            free(tmp);
                        }
                    } else {
                        len = strlen(value);
                        memcpy(datapos, value, len + 1);
                    }

                    datapos += len + 1;
                    offset += len + 1;
                }
            }
        } else {
            /* get the trailer count value */
            v->entry->count = trailer_size;
        }

        /* convert entry fields to network byte order */
        v->entry->tag = htonl(v->entry->tag);
        v->entry->type = htonl(v->entry->type);
        v->entry->offset = htonl(v->entry->offset);
        v->entry->count = htonl(v->entry->count);

        /* move to next entry */
        v->entry++;
    }

    return r;
}

/*
 * Validates an RPM header signature.  True if valid, false if sig is NULL or sig is invalid.
 */
bool
valid_header(struct rpmhdr *hdr)
{
    if (hdr == NULL) {
        return false;
    }

    if (hdr->magic != RPM_SIGNATURE_MAGIC) {
        warnx("magic value mismatch, not an RPM");
        return false;
    }

    if (hdr->reserved != RPM_SIGNATURE_RESERVED) {
        warnx("reserved value mismatch, not an RPM");
        return false;
    }

    return true;
}

/*
 * Read the data of the RPM header and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_header(const int fd, const char *dest_dir)
{
    uint32_t *buffer = NULL;
    struct rpmhdr *rawhdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *jvals = NULL;
    struct json_object *header = NULL;
    struct json_object *changelog = NULL;
    struct json_object *dependencies = NULL;

    if (fd < 0) {
        return NULL;
    }

    /* read in the header signature -- identifies the start of an RPM header block */
    rawhdr = read_header_signature(fd);

    if (rawhdr == NULL) {
        err(EXIT_FAILURE, "read_header_signature");
    }

    /* computed from header values */
    hdrinfo = mkhdrinfo(rawhdr, false);

    /* read in the entries */
    buffer = read_header_entries(fd, rawhdr, hdrinfo->hlen);

    if (buffer == NULL) {
        err(EXIT_FAILURE, "read_header_entries");
    }

    hdrinfo->estart = (struct rpmhdrentry *) &(buffer[2]);
    hdrinfo->datastart = (uint8_t *) (hdrinfo->estart + rawhdr->nentries);

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(rawhdr, hdrinfo->estart, hdrinfo->datastart);

    /* generate a JSON structure for the signature */
    header = generate_json(rawhdr, hdrinfo);

    /* dump all of the tags in the header block */
    jvals = generate_json_entries(rawhdr, hdrinfo, trailer, dest_dir, false);

    /* write the header to a file */
    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    /* build the dependencies object if dependency tags are present */
    dependencies = generate_dependencies(rawhdr, hdrinfo);

    if (dependencies != NULL) {
        json_object_object_add(header, RPM_DEPENDENCIES_DESC, dependencies);
    }

    /* build the changelog array if changelog tags are present */
    changelog = generate_changelog(rawhdr, hdrinfo);

    if (changelog != NULL) {
        json_object_object_add(header, RPM_CHANGELOG_DESC, changelog);
    }

    /* cleanup */
    free(hdrinfo);
    json_object_put(jvals);
    free(trailer);
    free(buffer);
    free(rawhdr);

    return header;
}

/*
 * Create a new header data structure for later writing to an RPM
 * output file.  Headers begin with the magic number, number of
 * records, and the size of the storage area.  The storage area is a
 * buffer of header index structs followed by the storage area for
 * that index record.  This function is really more of an
 * initialization of the header structure that you then follow up with
 * functions to add records to.
 *
 * Returns 0 on success, non-zero otherwise.  Pointers should be
 * passed in to structures the caller can use for the rpmhdr and
 * rpmhdrinfo.  Those will be allocated and modified by this function.
 * Caller must free memory associated with those structures.
 */
int
create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo)
{
    int r = 0;
    struct rpmhdr *s;
    struct rpmhdrinfo *v;
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *tags_copy = NULL;
    size_t totalsize = 0;
    int32_t trailer_index = -1;
    size_t trailer_size = 0;
    bool need_free_tags = false;
    size_t i = 0;

    if (data == NULL) {
        return -1;
    }

    /* allocate the two structures for the header */
    s = xalloc(sizeof(*s));
    v = xalloc(sizeof(*v));

    /* fill out the beginning with the magic and reserved values */
    s->magic = htonl(RPM_SIGNATURE_MAGIC);
    s->reserved = htonl(RPM_SIGNATURE_RESERVED);

    /* get the tags for this header */
    if (json_object_object_get_ex(data, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        free(s);
        free(v);
        return -1;
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** create_header: tags must be an array"));
        free(s);
        free(v);
        return -1;
    }

    /* Check if there's a changelog array that needs to be converted to tags */
    if (json_object_object_get_ex(data, RPM_CHANGELOG_DESC, &changelog)) {
        /* Create a mutable copy of the tags array */
        tags_copy = json_object_new_array();

        for (i = 0; i < json_object_array_length(tags); i++) {
            json_object_array_add(tags_copy, json_object_get(json_object_array_get_idx(tags, i)));
        }

        /* Add the changelog tags to the copy */
        add_changelog_tags(tags_copy, changelog);

        /* Use the copy for processing */
        tags = tags_copy;
        need_free_tags = true;
    }

    /* Check if there's a dependencies object that needs to be converted to tags */
    if (json_object_object_get_ex(data, RPM_DEPENDENCIES_DESC, &dependencies)) {
        /* Create a mutable copy if we haven't already */
        if (!need_free_tags) {
            tags_copy = json_object_new_array();

            for (i = 0; i < json_object_array_length(tags); i++) {
                json_object_array_add(tags_copy, json_object_get(json_object_array_get_idx(tags, i)));
            }

            tags = tags_copy;
            need_free_tags = true;
        }

        /* Add the dependency tags to the copy */
        add_dependency_tags(tags_copy, dependencies);
    }

    /* number of header index entries */
    s->nentries = json_object_array_length(tags);

    /* allocate an array for the header index entries */
    v->estart = xcalloc(s->nentries, sizeof(*(v->estart)));
    v->entry = v->estart;
    s->nentries = htonl(s->nentries);

    /*
     * total size is just the sequential data, NOT including trailer
     * trailer offset will point past the end of data
     */
    totalsize = get_data_buffer_size(tags, &trailer_index, &trailer_size);

    /* allocate the data buffer */
    v->datastart = xcalloc(totalsize, sizeof(uint8_t));

    /*
     * set the data size in the header
     * nbytes includes trailer size even though trailer data isn't
     * written
     */
    if (trailer_index >= 0) {
        s->nbytes = htonl(totalsize + trailer_size);
    } else {
        s->nbytes = htonl(totalsize);
    }

    /* walk the header tags and add them to the values structure */
    r = add_header_tags(tags, v, totalsize, trailer_index, trailer_size);

    /* cleanup temporary tags array if we created one */
    if (need_free_tags) {
        json_object_put(tags);
    }

    /* ensure caller gets the header and data */
    *hdr = s;
    *hdrinfo = v;

    return r;
}

/* Returns true if there is a trailer in the specified header */
bool
has_trailer(const uint32_t nentries, const struct rpmhdrentry *estart)
{
    uint32_t i = 0;

    if (nentries == 0 || estart == NULL) {
        return false;
    }

    for (i = 0; i < nentries; i++) {
        if (ntohl(estart[i].tag) == HEADER_SIGNATURES || ntohl(estart[i].tag) == HEADER_IMMUTABLE) {
            return true;
        }
    }

    return false;
}

/*
 * Looks for the trailer data in the header tags and if found, does a
 * base64 decode and stores that in the trailer_data parameter and the
 * size in the trailer_size parameter.  Returns 0 on success, non-zero
 * on error.  Caller must free trailer_data.
 */
int
get_trailer_data(const struct json_object *data, uint8_t **trailer_data, size_t *trailer_size)
{
    size_t i = 0;
    int r = 0;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *tobj = NULL;
    struct json_object *vobj = NULL;
    const char *trailer_value = NULL;

    if (data == NULL || trailer_data == NULL || trailer_size == NULL) {
        return -1;
    }

    if (json_object_object_get_ex(data, RPM_ENTRY_TAGS_DESC, &tags) == 1) {
        for (i = 0; i < json_object_array_length(tags); i++) {
            entry = json_object_array_get_idx(tags, i);

            if (json_object_object_get_ex(entry, RPM_ENTRY_TRAILER_DESC, &tobj) == 1) {
                if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &vobj) == 1) {
                    trailer_value = json_object_get_string(vobj);
                    r = rpmBase64Decode(trailer_value, (void **) trailer_data, trailer_size);

                    if (r == 0 && *trailer_size == 16) {
                        return 0;
                    }

                    free(*trailer_data);
                    *trailer_data = NULL;
                }

                break;
            }
        }
    }

    return -1;
}
