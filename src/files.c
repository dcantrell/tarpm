/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>

#include "tarpm.h"

/*
 * Generate a "files" array from the DIRNAMES, BASENAMES, and DIRINDEXES tags.
 * Returns a JSON array where each entry is {"path": "/full/path/to/file"}.
 * Returns NULL if the required tags are not found.
 */
struct json_object *
generate_files(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t tag = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    rpmTagType datatype = 0;
    struct rpmhdrentry *hdrentry = NULL;
    uint8_t *data = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    char **dirnames = NULL;
    uint32_t ndirnames = 0;
    char **basenames = NULL;
    uint32_t nbasenames = 0;
    uint32_t *dirindexes = NULL;
    uint32_t ndirindexes = 0;
    uint8_t *p = NULL;
    uint32_t dirindex = 0;
    char *path = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;

    /* First pass: collect the three file list arrays */
    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);
        data = hdrinfo->datastart + offset;

        if (tag == RPMTAG_DIRNAMES && datatype == RPM_STRING_ARRAY_TYPE) {
            ndirnames = count;
            dirnames = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                dirnames[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_BASENAMES && datatype == RPM_STRING_ARRAY_TYPE) {
            nbasenames = count;
            basenames = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                basenames[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_DIRINDEXES && datatype == RPM_INT32_TYPE) {
            ndirindexes = count;
            dirindexes = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&dirindex, p, sizeof(uint32_t));
                dirindexes[j] = ntohl(dirindex);
                p += sizeof(uint32_t);
            }
        }
    }

    /* No file list found */
    if (dirnames == NULL || basenames == NULL || dirindexes == NULL) {
        if (dirnames) {
            for (i = 0; i < ndirnames; i++) {
                free(dirnames[i]);
            }
            free(dirnames);
        }

        if (basenames) {
            for (i = 0; i < nbasenames; i++) {
                free(basenames[i]);
            }
            free(basenames);
        }

        free(dirindexes);
        return NULL;
    }

    /* Verify arrays have consistent lengths */
    if (nbasenames != ndirindexes) {
        warnx(_("*** file list arrays have mismatched lengths"));

        for (i = 0; i < ndirnames; i++) {
            free(dirnames[i]);
        }
        free(dirnames);

        for (i = 0; i < nbasenames; i++) {
            free(basenames[i]);
        }
        free(basenames);

        free(dirindexes);
        return NULL;
    }

    /* Build the files array */
    files = json_object_new_array();

    for (j = 0; j < nbasenames; j++) {
        /* Verify dirindex is valid */
        if (dirindexes[j] >= ndirnames) {
            warnx(_("*** invalid dirindex %u (max %u)"), dirindexes[j], ndirnames - 1);
            continue;
        }

        /* Combine dirname and basename */
        xasprintf(&path, "%s%s", dirnames[dirindexes[j]], basenames[j]);

        /* Create file entry with path */
        file = json_object_new_object();
        json_object_object_add(file, "path", json_object_new_string(path));
        json_object_array_add(files, file);

        free(path);
    }

    /* Cleanup */
    for (i = 0; i < ndirnames; i++) {
        free(dirnames[i]);
    }
    free(dirnames);

    for (i = 0; i < nbasenames; i++) {
        free(basenames[i]);
    }
    free(basenames);

    free(dirindexes);

    return files;
}

/*
 * Reconstruct the three file list tag entries from the files array.
 * Adds the three tag entries (DIRNAMES, BASENAMES, DIRINDEXES) to the provided tags array.
 */
void
add_file_list_tags(struct json_object *tags, struct json_object *files)
{
    size_t i = 0;
    size_t j = 0;
    size_t count = 0;
    size_t ndirs = 0;
    struct json_object *file = NULL;
    struct json_object *path_obj = NULL;
    struct json_object *dirnames = NULL;
    struct json_object *basenames = NULL;
    struct json_object *dirindexes = NULL;
    struct json_object *tag = NULL;
    const char *path = NULL;
    const char *basename = NULL;
    const char *dirname = NULL;
    char *dirname_copy = NULL;
    char *separator = NULL;
    char **unique_dirs = NULL;
    int dirindex = 0;
    bool found = false;

    if (tags == NULL || files == NULL) {
        return;
    }

    if (json_object_get_type(files) != json_type_array) {
        return;
    }

    count = json_object_array_length(files);

    if (count == 0) {
        return;
    }

    /* Allocate arrays for unique directory tracking */
    unique_dirs = xcalloc(count, sizeof(char *));

    /* Create the three arrays */
    dirnames = json_object_new_array();
    basenames = json_object_new_array();
    dirindexes = json_object_new_array();

    /* Process each file entry */
    for (i = 0; i < count; i++) {
        file = json_object_array_get_idx(files, i);

        if (!json_object_object_get_ex(file, "path", &path_obj)) {
            continue;
        }

        path = json_object_get_string(path_obj);

        /* Find the last separator to split dirname and basename */
        separator = strrchr(path, '/');

        if (separator == NULL) {
            /* No directory separator, use current directory */
            dirname = "./";
            basename = path;
        } else {
            /* Split into dirname and basename */
            basename = separator + 1;

            /* Extract dirname (including trailing slash) */
            dirname_copy = xalloc(separator - path + 2);
            memcpy(dirname_copy, path, separator - path + 1);
            dirname_copy[separator - path + 1] = '\0';
            dirname = dirname_copy;
        }

        /* Find or add dirname to unique_dirs */
        dirindex = -1;
        found = false;

        for (j = 0; j < ndirs; j++) {
            if (strcmp(unique_dirs[j], dirname) == 0) {
                dirindex = j;
                found = true;
                break;
            }
        }

        if (!found) {
            /* New directory */
            unique_dirs[ndirs] = strdup(dirname);
            dirindex = ndirs;
            ndirs++;
        }

        /* Add basename and dirindex */
        json_object_array_add(basenames, json_object_new_string(basename));
        json_object_array_add(dirindexes, json_object_new_int(dirindex));

        if (dirname_copy) {
            free(dirname_copy);
            dirname_copy = NULL;
        }
    }

    /* Build the dirnames array from unique_dirs */
    for (i = 0; i < ndirs; i++) {
        json_object_array_add(dirnames, json_object_new_string(unique_dirs[i]));
        free(unique_dirs[i]);
    }

    free(unique_dirs);

    /* Add BASENAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string("Basenames"));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string("string array"));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, basenames);
    json_object_array_add(tags, tag);

    /* Add DIRINDEXES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string("Dirindexes"));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string("int32"));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, dirindexes);
    json_object_array_add(tags, tag);

    /* Add DIRNAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string("Dirnames"));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string("string array"));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, dirnames);
    json_object_array_add(tags, tag);
}

/*
 * Returns true if the tag is one of the file list tags.
 */
bool
is_file_list_tag(rpmTagVal tag)
{
    if (tag == RPMTAG_DIRNAMES || tag == RPMTAG_BASENAMES || tag == RPMTAG_DIRINDEXES) {
        return true;
    }

    return false;
}
