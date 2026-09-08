/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <err.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>

#include "tarpm.h"

/*
 * Generate a "files" array from the DIRNAMES, BASENAMES, and DIRINDEXES tags.
 * Returns a JSON array where each entry is {"path": "/full/path/to/file"} and
 * optionally "size" for regular files.
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
    uint32_t *filesizes = NULL;
    uint32_t nfilesizes = 0;
    uint16_t *filemodes = NULL;
    uint32_t nfilemodes = 0;
    uint32_t *filemtimes = NULL;
    uint32_t nfilemtimes = 0;
    char **fileusernames = NULL;
    uint32_t nfileusernames = 0;
    char **filegroupnames = NULL;
    uint32_t nfilegroupnames = 0;
    uint16_t *filerdevs = NULL;
    uint32_t nfilerdevs = 0;
    uint32_t *filedevices = NULL;
    uint32_t nfiledevices = 0;
    char **filedigests = NULL;
    uint32_t nfiledigests = 0;
    char **filelinktos = NULL;
    uint32_t nfilelinktos = 0;
    uint32_t *fileinodes = NULL;
    uint32_t nfileinodes = 0;
    uint32_t *fileclass = NULL;
    uint32_t nfileclass = 0;
    char **classdict = NULL;
    uint32_t nclassdict = 0;
    uint8_t *p = NULL;
    uint32_t dirindex = 0;
    uint32_t size_val = 0;
    uint16_t mode_val = 0;
    uint32_t mtime_val = 0;
    uint16_t rdev_val = 0;
    uint32_t device_val = 0;
    uint32_t inode_val = 0;
    uint32_t class_val = 0;
    char *path = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;

    /* First pass: collect the file list arrays */
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
        } else if (tag == RPMTAG_FILESIZES && datatype == RPM_INT32_TYPE) {
            nfilesizes = count;
            filesizes = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&size_val, p, sizeof(uint32_t));
                filesizes[j] = ntohl(size_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEMODES && datatype == RPM_INT16_TYPE) {
            nfilemodes = count;
            filemodes = xalloc(count * sizeof(uint16_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&mode_val, p, sizeof(uint16_t));
                filemodes[j] = ntohs(mode_val);
                p += sizeof(uint16_t);
            }
        } else if (tag == RPMTAG_FILEMTIMES && datatype == RPM_INT32_TYPE) {
            nfilemtimes = count;
            filemtimes = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&mtime_val, p, sizeof(uint32_t));
                filemtimes[j] = ntohl(mtime_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEUSERNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            nfileusernames = count;
            fileusernames = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                fileusernames[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILEGROUPNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            nfilegroupnames = count;
            filegroupnames = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                filegroupnames[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILERDEVS && datatype == RPM_INT16_TYPE) {
            nfilerdevs = count;
            filerdevs = xalloc(count * sizeof(uint16_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&rdev_val, p, sizeof(uint16_t));
                filerdevs[j] = ntohs(rdev_val);
                p += sizeof(uint16_t);
            }
        } else if (tag == RPMTAG_FILEDEVICES && datatype == RPM_INT32_TYPE) {
            nfiledevices = count;
            filedevices = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&device_val, p, sizeof(uint32_t));
                filedevices[j] = ntohl(device_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEDIGESTS && datatype == RPM_STRING_ARRAY_TYPE) {
            nfiledigests = count;
            filedigests = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                filedigests[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILELINKTOS && datatype == RPM_STRING_ARRAY_TYPE) {
            nfilelinktos = count;
            filelinktos = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                filelinktos[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILEINODES && datatype == RPM_INT32_TYPE) {
            nfileinodes = count;
            fileinodes = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&inode_val, p, sizeof(uint32_t));
                fileinodes[j] = ntohl(inode_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILECLASS && datatype == RPM_INT32_TYPE) {
            nfileclass = count;
            fileclass = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&class_val, p, sizeof(uint32_t));
                fileclass[j] = ntohl(class_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_CLASSDICT && datatype == RPM_STRING_ARRAY_TYPE) {
            nclassdict = count;
            classdict = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                classdict[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
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

        if (fileusernames) {
            for (i = 0; i < nfileusernames; i++) {
                free(fileusernames[i]);
            }

            free(fileusernames);
        }

        if (filegroupnames) {
            for (i = 0; i < nfilegroupnames; i++) {
                free(filegroupnames[i]);
            }

            free(filegroupnames);
        }

        if (filedigests) {
            for (i = 0; i < nfiledigests; i++) {
                free(filedigests[i]);
            }

            free(filedigests);
        }

        if (filelinktos) {
            for (i = 0; i < nfilelinktos; i++) {
                free(filelinktos[i]);
            }

            free(filelinktos);
        }

        if (classdict) {
            for (i = 0; i < nclassdict; i++) {
                free(classdict[i]);
            }

            free(classdict);
        }

        free(dirindexes);
        free(filesizes);
        free(filemodes);
        free(filemtimes);
        free(filerdevs);
        free(filedevices);
        free(fileinodes);
        free(fileclass);
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

        if (fileusernames) {
            for (i = 0; i < nfileusernames; i++) {
                free(fileusernames[i]);
            }

            free(fileusernames);
        }

        if (filegroupnames) {
            for (i = 0; i < nfilegroupnames; i++) {
                free(filegroupnames[i]);
            }

            free(filegroupnames);
        }

        if (filedigests) {
            for (i = 0; i < nfiledigests; i++) {
                free(filedigests[i]);
            }

            free(filedigests);
        }

        if (filelinktos) {
            for (i = 0; i < nfilelinktos; i++) {
                free(filelinktos[i]);
            }

            free(filelinktos);
        }

        if (classdict) {
            for (i = 0; i < nclassdict; i++) {
                free(classdict[i]);
            }

            free(classdict);
        }

        free(dirindexes);
        free(filesizes);
        free(filemodes);
        free(filemtimes);
        free(filerdevs);
        free(filedevices);
        free(fileinodes);
        free(fileclass);
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

        /*
         * Add size for regular files and symlinks.  RPM stores the
         * length of a symlink's target string in FILESIZES, and the
         * payload reader consumes that many bytes for the target, so
         * the value must be preserved for symlinks too.
         */
        if (filesizes && filemodes && j < nfilesizes && j < nfilemodes) {
            if (S_ISREG(filemodes[j]) || S_ISLNK(filemodes[j])) {
                json_object_object_add(file, "size", json_object_new_int64(filesizes[j]));
            }
        }

        /* Add mode if available (as octal string of permission bits only) */
        if (filemodes && j < nfilemodes) {
            char mode_str[5];
            snprintf(mode_str, sizeof(mode_str), "%04o", filemodes[j] & ALLPERMS);
            json_object_object_add(file, "mode", json_object_new_string(mode_str));
        }

        /* Add mtime if available (as ISO 8601 timestamp) */
        if (filemtimes && j < nfilemtimes) {
            time_t mtime = (time_t) filemtimes[j];
            struct tm *tm_info = gmtime(&mtime);
            char mtime_str[32];

            if (tm_info != NULL) {
                strftime(mtime_str, sizeof(mtime_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                json_object_object_add(file, "mtime", json_object_new_string(mtime_str));
            }
        }

        /* Add user if available */
        if (fileusernames && j < nfileusernames) {
            json_object_object_add(file, "user", json_object_new_string(fileusernames[j]));
        }

        /* Add group if available */
        if (filegroupnames && j < nfilegroupnames) {
            json_object_object_add(file, "group", json_object_new_string(filegroupnames[j]));
        }

        /* Add rdev if available (only for device nodes with non-zero values) */
        if (filerdevs && filemodes && j < nfilerdevs && j < nfilemodes) {
            if ((S_ISCHR(filemodes[j]) || S_ISBLK(filemodes[j])) && filerdevs[j] != 0) {
                json_object_object_add(file, "rdev", json_object_new_int(filerdevs[j]));
            }
        }

        /* Add device if available */
        if (filedevices && j < nfiledevices) {
            json_object_object_add(file, "device", json_object_new_int64(filedevices[j]));
        }

        /*
         * Add digest for regular files that have one.  RPM stores an
         * empty string for entries without a digest (directories,
         * symlinks, device nodes, etc.), so only emit the key when the
         * digest is non-empty.
         */
        if (filedigests && j < nfiledigests && filedigests[j][0] != '\0') {
            json_object_object_add(file, "digest", json_object_new_string(filedigests[j]));
        }

        /*
         * Add linkto for symbolic links.  RPM stores an empty string
         * for entries that are not symlinks, so only emit the key when
         * the target is non-empty.
         */
        if (filelinktos && j < nfilelinktos && filelinktos[j][0] != '\0') {
            json_object_object_add(file, "linkto", json_object_new_string(filelinktos[j]));
        }

        /*
         * Add inode for every entry.  RPM assigns these numbers itself
         * (starting at 1 and incrementing) to track hard links; files
         * that share an inode number are hard links of one another.
         */
        if (fileinodes && j < nfileinodes) {
            json_object_object_add(file, "inode", json_object_new_int(fileinodes[j]));
        }

        /*
         * Add class for entries that have one.  FILECLASS holds an index
         * into the CLASSDICT string array (libmagic-style type
         * descriptions), so resolve the index to its string here.  Skip
         * the key when the resolved class is an empty string.
         */
        if (fileclass && classdict && j < nfileclass && fileclass[j] < nclassdict && classdict[fileclass[j]][0] != '\0') {
            json_object_object_add(file, "class", json_object_new_string(classdict[fileclass[j]]));
        }

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

    if (fileusernames) {
        for (i = 0; i < nfileusernames; i++) {
            free(fileusernames[i]);
        }

        free(fileusernames);
    }

    if (filegroupnames) {
        for (i = 0; i < nfilegroupnames; i++) {
            free(filegroupnames[i]);
        }

        free(filegroupnames);
    }

    if (filedigests) {
        for (i = 0; i < nfiledigests; i++) {
            free(filedigests[i]);
        }

        free(filedigests);
    }

    if (filelinktos) {
        for (i = 0; i < nfilelinktos; i++) {
            free(filelinktos[i]);
        }

        free(filelinktos);
    }

    if (classdict) {
        for (i = 0; i < nclassdict; i++) {
            free(classdict[i]);
        }

        free(classdict);
    }

    free(dirindexes);
    free(filesizes);
    free(filemodes);
    free(filemtimes);
    free(filerdevs);
    free(filedevices);
    free(fileinodes);
    free(fileclass);

    return files;
}

/*
 * Reconstruct the file list tag entries from the files array.
 * Adds the file list tag entries (DIRNAMES, BASENAMES, DIRINDEXES, FILESIZES, FILEMODES, FILEMTIMES)
 * to the provided tags array.
 */
void
add_file_list_tags(struct json_object *tags, struct json_object *files, const char *input_dir, const char *payload_subdir)
{
    size_t i = 0;
    size_t j = 0;
    size_t count = 0;
    size_t ndirs = 0;
    struct json_object *file = NULL;
    struct json_object *path_obj = NULL;
    struct json_object *size_obj = NULL;
    struct json_object *mode_obj = NULL;
    struct json_object *mtime_obj = NULL;
    struct json_object *user_obj = NULL;
    struct json_object *group_obj = NULL;
    struct json_object *rdev_obj = NULL;
    struct json_object *device_obj = NULL;
    struct json_object *digest_obj = NULL;
    struct json_object *linkto_obj = NULL;
    struct json_object *inode_obj = NULL;
    struct json_object *class_obj = NULL;
    struct json_object *dirnames = NULL;
    struct json_object *basenames = NULL;
    struct json_object *dirindexes = NULL;
    struct json_object *filesizes = NULL;
    struct json_object *filemodes = NULL;
    struct json_object *filemtimes = NULL;
    struct json_object *fileusernames = NULL;
    struct json_object *filegroupnames = NULL;
    struct json_object *filerdevs = NULL;
    struct json_object *filedevices = NULL;
    struct json_object *filedigests = NULL;
    struct json_object *filelinktos = NULL;
    struct json_object *fileinodes = NULL;
    struct json_object *fileclass = NULL;
    struct json_object *classdict = NULL;
    struct json_object *tag = NULL;
    const char *path = NULL;
    const char *basename = NULL;
    const char *dirname = NULL;
    const char *mode_str = NULL;
    const char *mtime_str = NULL;
    const char *user_str = NULL;
    const char *group_str = NULL;
    const char *digest_str = NULL;
    const char *linkto_str = NULL;
    const char *class_str = NULL;
    char *dirname_copy = NULL;
    const char *separator = NULL;
    char *file_path = NULL;
    char **unique_dirs = NULL;
    char **unique_classes = NULL;
    size_t nclasses = 0;
    struct stat sb;
    struct tm tm_info;
    int dirindex = 0;
    int64_t size = 0;
    int mode = 0;
    int perms = 0;
    int rdev = 0;
    int64_t device = 0;
    int inode = 0;
    int classindex = 0;
    time_t mtime = 0;
    bool found = false;
    bool class_found = false;

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

    /* Allocate arrays for unique directory and class tracking */
    unique_dirs = xcalloc(count, sizeof(char *));
    unique_classes = xcalloc(count, sizeof(char *));

    /* Create the arrays */
    dirnames = json_object_new_array();
    basenames = json_object_new_array();
    dirindexes = json_object_new_array();
    filesizes = json_object_new_array();
    filemodes = json_object_new_array();
    filemtimes = json_object_new_array();
    fileusernames = json_object_new_array();
    filegroupnames = json_object_new_array();
    filerdevs = json_object_new_array();
    filedevices = json_object_new_array();
    filedigests = json_object_new_array();
    filelinktos = json_object_new_array();
    fileinodes = json_object_new_array();
    fileclass = json_object_new_array();
    classdict = json_object_new_array();

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

        /* Add size (regular files have size, non-files get 0) */
        if (json_object_object_get_ex(file, "size", &size_obj)) {
            size = json_object_get_int64(size_obj);
        } else {
            size = 0;
        }

        json_object_array_add(filesizes, json_object_new_int64(size));

        /* Reconstruct full mode from permission bits and actual file type */
        mode = 0;

        if (json_object_object_get_ex(file, "mode", &mode_obj)) {
            /* Parse octal permission string */
            mode_str = json_object_get_string(mode_obj);
            perms = (int) strtol(mode_str, NULL, 8);

            /* Determine file type */
            if (input_dir != NULL && payload_subdir != NULL) {
                /* Strip leading slash from path for payload lookup */
                const char *relative_path = (path[0] == '/') ? path + 1 : path;
                file_path = joinpath(input_dir, payload_subdir, relative_path, NULL);

                if (lstat(file_path, &sb) == 0) {
                    /* Combine file type from stat with permissions from JSON */
                    mode = (sb.st_mode & ~ALLPERMS) | (perms & ALLPERMS);
                } else {
                    /* File doesn't exist in payload (e.g., 0-byte file or directory) */
                    /* Use heuristic: if entry has "size" field, it's a regular file */
                    if (json_object_object_get_ex(file, "size", NULL)) {
                        mode = S_IFREG | (perms & ALLPERMS);
                    } else {
                        /* No size field, assume directory */
                        mode = S_IFDIR | (perms & ALLPERMS);
                    }
                }

                free(file_path);
            } else {
                /* No input_dir provided, use permissions only (assume regular file) */
                mode = S_IFREG | (perms & ALLPERMS);
            }
        }

        json_object_array_add(filemodes, json_object_new_int(mode));

        /* Parse mtime from ISO 8601 timestamp string */
        mtime = 0;

        if (json_object_object_get_ex(file, "mtime", &mtime_obj)) {
            mtime_str = json_object_get_string(mtime_obj);
            memset(&tm_info, 0, sizeof(struct tm));

            if (strptime(mtime_str, "%Y-%m-%dT%H:%M:%SZ", &tm_info) != NULL) {
                mtime = timegm(&tm_info);
            }
        }

        json_object_array_add(filemtimes, json_object_new_int64(mtime));

        /* Extract user if available */
        user_str = NULL;

        if (json_object_object_get_ex(file, "user", &user_obj)) {
            user_str = json_object_get_string(user_obj);
        }

        if (user_str != NULL) {
            json_object_array_add(fileusernames, json_object_new_string(user_str));
        } else {
            json_object_array_add(fileusernames, json_object_new_string("root"));
        }

        /* Extract group if available */
        group_str = NULL;

        if (json_object_object_get_ex(file, "group", &group_obj)) {
            group_str = json_object_get_string(group_obj);
        }

        if (group_str != NULL) {
            json_object_array_add(filegroupnames, json_object_new_string(group_str));
        } else {
            json_object_array_add(filegroupnames, json_object_new_string("root"));
        }

        /* Extract rdev if available */
        rdev = 0;

        if (json_object_object_get_ex(file, "rdev", &rdev_obj)) {
            rdev = json_object_get_int(rdev_obj);
        }

        json_object_array_add(filerdevs, json_object_new_int(rdev));

        /* Extract device if available */
        device = 0;

        if (json_object_object_get_ex(file, "device", &device_obj)) {
            device = json_object_get_int64(device_obj);
        }

        json_object_array_add(filedevices, json_object_new_int64(device));

        /*
         * Extract digest if available.  Entries without a "digest" key
         * (directories, symlinks, device nodes, etc.) carry an empty
         * string so the array stays parallel to the file list.
         */
        digest_str = NULL;

        if (json_object_object_get_ex(file, "digest", &digest_obj)) {
            digest_str = json_object_get_string(digest_obj);
        }

        if (digest_str != NULL) {
            json_object_array_add(filedigests, json_object_new_string(digest_str));
        } else {
            json_object_array_add(filedigests, json_object_new_string(""));
        }

        /*
         * Extract linkto if available.  Entries without a "linkto" key
         * (everything that is not a symlink) carry an empty string so
         * the array stays parallel to the file list.
         */
        linkto_str = NULL;

        if (json_object_object_get_ex(file, "linkto", &linkto_obj)) {
            linkto_str = json_object_get_string(linkto_obj);
        }

        if (linkto_str != NULL) {
            json_object_array_add(filelinktos, json_object_new_string(linkto_str));
        } else {
            json_object_array_add(filelinktos, json_object_new_string(""));
        }

        /*
         * Extract inode.  When the "inode" key is present its value is
         * used verbatim, which preserves the hard link grouping recorded
         * by RPM.  When absent, fall back to a sequential number (RPM
         * numbers inodes starting at 1) so every entry maps to one.
         */
        if (json_object_object_get_ex(file, "inode", &inode_obj)) {
            inode = json_object_get_int(inode_obj);
        } else {
            inode = (int) (i + 1);
        }

        json_object_array_add(fileinodes, json_object_new_int(inode));

        /*
         * Extract class and rebuild the CLASSDICT/FILECLASS pair the way
         * librpm does: CLASSDICT holds the unique class strings in order
         * of first appearance and FILECLASS holds each file's index into
         * it.  Entries without a "class" key use an empty string.
         */
        class_str = "";

        if (json_object_object_get_ex(file, "class", &class_obj)) {
            class_str = json_object_get_string(class_obj);
        }

        classindex = -1;
        class_found = false;

        for (j = 0; j < nclasses; j++) {
            if (strcmp(unique_classes[j], class_str) == 0) {
                classindex = j;
                class_found = true;
                break;
            }
        }

        if (!class_found) {
            unique_classes[nclasses] = strdup(class_str);
            classindex = nclasses;
            nclasses++;
        }

        json_object_array_add(fileclass, json_object_new_int(classindex));

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

    /* Build the classdict array from unique_classes */
    for (i = 0; i < nclasses; i++) {
        json_object_array_add(classdict, json_object_new_string(unique_classes[i]));
        free(unique_classes[i]);
    }

    free(unique_classes);

    /* Add RPMTAG_BASENAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_BASENAMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, basenames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_DIRINDEXES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DIRINDEXES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, dirindexes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_DIRNAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DIRNAMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, dirnames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILESIZES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILESIZES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filesizes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEMODES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEMODES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT16_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filemodes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEMTIMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEMTIMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filemtimes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEUSERNAME tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEUSERNAME)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, fileusernames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEGROUPNAME tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEGROUPNAME)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filegroupnames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILERDEVS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILERDEVS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT16_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filerdevs);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEDEVICES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDEVICES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filedevices);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEDIGESTS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDIGESTS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filedigests);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILELINKTOS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILELINKTOS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filelinktos);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEINODES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEINODES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, fileinodes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILECLASS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILECLASS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, fileclass);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_CLASSDICT tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_CLASSDICT)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, classdict);
    json_object_array_add(tags, tag);
}

/*
 * Returns true if the tag is one of the file list tags.
 */
bool
is_file_list_tag(rpmTagVal tag)
{
    switch (tag) {
        case RPMTAG_DIRNAMES:
        case RPMTAG_BASENAMES:
        case RPMTAG_DIRINDEXES:
        case RPMTAG_FILESIZES:
        case RPMTAG_FILEMODES:
        case RPMTAG_FILEMTIMES:
        case RPMTAG_FILEUSERNAME:
        case RPMTAG_FILEGROUPNAME:
        case RPMTAG_FILERDEVS:
        case RPMTAG_FILEDEVICES:
        case RPMTAG_FILEDIGESTS:
        case RPMTAG_FILELINKTOS:
        case RPMTAG_FILEINODES:
        case RPMTAG_FILECLASS:
        case RPMTAG_CLASSDICT:
            return true;
    }

    return false;
}
