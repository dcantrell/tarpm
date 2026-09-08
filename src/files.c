/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <err.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmfc.h>
#include <json.h>

#include "tarpm.h"

/* Names used for the file color bits in the "files" array */
#define FILE_COLOR_ELF32 "Elf32"
#define FILE_COLOR_ELF64 "Elf64"

/*
 * Holds the raw file lists read out of the header while
 * generate_files() restructures them in to the "files" JSON array.
 * Every list carries one entry per file in the same order as the
 * header tag it came from.
 */
struct file_metadata {
    str_list_t *dirnames;
    str_list_t *basenames;
    uint32_list_t *dirindexes;
    uint32_list_t *filesizes;
    uint32_list_t *filemodes;
    uint32_list_t *filemtimes;
    str_list_t *fileusernames;
    str_list_t *filegroupnames;
    uint32_list_t *filerdevs;
    uint32_list_t *filedevices;
    str_list_t *filedigests;
    str_list_t *filelinktos;
    uint32_list_t *fileinodes;
    uint32_list_t *fileclass;
    str_list_t *classdict;
    str_list_t *filelangs;
    uint32_list_t *filecolors;
};

/*
 * Free all of the lists held in a struct file_metadata.  String lists go
 * through list_free() and the integer lists through uint32_list_free().
 * Safe to call with a NULL pointer.
 */
static void
free_file_metadata(struct file_metadata *fmd)
{
    if (fmd == NULL) {
        return;
    }

    list_free(fmd->dirnames, free);
    list_free(fmd->basenames, free);
    list_free(fmd->fileusernames, free);
    list_free(fmd->filegroupnames, free);
    list_free(fmd->filedigests, free);
    list_free(fmd->filelinktos, free);
    list_free(fmd->classdict, free);
    list_free(fmd->filelangs, free);
    uint32_list_free(fmd->dirindexes);
    uint32_list_free(fmd->filesizes);
    uint32_list_free(fmd->filemodes);
    uint32_list_free(fmd->filemtimes);
    uint32_list_free(fmd->filerdevs);
    uint32_list_free(fmd->filedevices);
    uint32_list_free(fmd->fileinodes);
    uint32_list_free(fmd->fileclass);
    uint32_list_free(fmd->filecolors);
    return;
}

/*
 * Convert a FILECOLORS value in to an array of color name strings.
 * Bit 0 marks a 32-bit ELF object and bit 1 marks a 64-bit ELF object.
 * Any remaining bits have no name, so they are carried as a single
 * decimal number to keep the value intact across a round trip.
 * Returns NULL for a color of zero, which callers use to omit the key.
 */
static struct json_object *
color_names(uint32_t color)
{
    uint32_t rest = 0;
    char *s = NULL;
    struct json_object *names = NULL;

    if (color == 0) {
        return NULL;
    }

    names = json_object_new_array();
    rest = color;

    if (color & RPMFC_ELF32) {
        json_object_array_add(names, json_object_new_string(FILE_COLOR_ELF32));
        rest &= ~((uint32_t) RPMFC_ELF32);
    }

    if (color & RPMFC_ELF64) {
        json_object_array_add(names, json_object_new_string(FILE_COLOR_ELF64));
        rest &= ~((uint32_t) RPMFC_ELF64);
    }

    if (rest != 0) {
        xasprintf(&s, "%u", rest);
        json_object_array_add(names, json_object_new_string(s));
        free(s);
    }

    return names;
}

/*
 * Convert an array of color name strings back in to a FILECOLORS
 * value.  Names that color_names() had no name for come back as a
 * decimal number.  Returns zero for a missing or empty array.
 */
static uint32_t
color_value(struct json_object *names)
{
    size_t i = 0;
    size_t len = 0;
    uint32_t color = 0;
    const char *s = NULL;
    char *end = NULL;
    unsigned long value = 0;

    if (names == NULL || json_object_get_type(names) != json_type_array) {
        return 0;
    }

    len = json_object_array_length(names);

    for (i = 0; i < len; i++) {
        s = json_object_get_string(json_object_array_get_idx(names, i));

        if (s == NULL) {
            continue;
        }

        if (!strcmp(s, FILE_COLOR_ELF32)) {
            color |= RPMFC_ELF32;
        } else if (!strcmp(s, FILE_COLOR_ELF64)) {
            color |= RPMFC_ELF64;
        } else {
            errno = 0;
            value = strtoul(s, &end, 10);

            if (errno != 0 || end == s || *end != '\0') {
                warnx(_("*** unknown file color: %s"), s);
                continue;
            }

            color |= (uint32_t) value;
        }
    }

    return color;
}

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
    struct file_metadata fmd = { 0 };
    uint8_t *p = NULL;
    uint32_t val32 = 0;
    uint16_t val16 = 0;
    uint32_t ndirnames = 0;
    const char *dirname = NULL;
    const char *classname = NULL;
    str_entry_t *basename = NULL;
    str_entry_t *username = NULL;
    str_entry_t *groupname = NULL;
    str_entry_t *digest = NULL;
    str_entry_t *linkto = NULL;
    str_entry_t *filelang = NULL;
    str_entry_t *lang = NULL;
    str_list_t *langs = NULL;
    struct json_object *langs_array = NULL;
    struct json_object *colors_array = NULL;
    uint32_entry_t *dirindex = NULL;
    uint32_entry_t *filesize = NULL;
    uint32_entry_t *filemode = NULL;
    uint32_entry_t *filemtime = NULL;
    uint32_entry_t *filerdev = NULL;
    uint32_entry_t *filedevice = NULL;
    uint32_entry_t *fileinode = NULL;
    uint32_entry_t *fileclass = NULL;
    uint32_entry_t *filecolor = NULL;
    char mode_str[5];
    char mtime_str[32];
    time_t mtime = 0;
    struct tm *tm_info = NULL;
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
            p = data;

            for (j = 0; j < count; j++) {
                fmd.dirnames = list_add(fmd.dirnames, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_BASENAMES && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.basenames = list_add(fmd.basenames, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_DIRINDEXES && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.dirindexes = uint32_list_add(fmd.dirindexes, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILESIZES && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filesizes = uint32_list_add(fmd.filesizes, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEMODES && datatype == RPM_INT16_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val16, p, sizeof(uint16_t));
                fmd.filemodes = uint32_list_add(fmd.filemodes, ntohs(val16));
                p += sizeof(uint16_t);
            }
        } else if (tag == RPMTAG_FILEMTIMES && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filemtimes = uint32_list_add(fmd.filemtimes, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEUSERNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.fileusernames = list_add(fmd.fileusernames, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILEGROUPNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.filegroupnames = list_add(fmd.filegroupnames, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILERDEVS && datatype == RPM_INT16_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val16, p, sizeof(uint16_t));
                fmd.filerdevs = uint32_list_add(fmd.filerdevs, ntohs(val16));
                p += sizeof(uint16_t);
            }
        } else if (tag == RPMTAG_FILEDEVICES && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filedevices = uint32_list_add(fmd.filedevices, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEDIGESTS && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.filedigests = list_add(fmd.filedigests, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILELINKTOS && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.filelinktos = list_add(fmd.filelinktos, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILEINODES && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.fileinodes = uint32_list_add(fmd.fileinodes, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILECLASS && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.fileclass = uint32_list_add(fmd.fileclass, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_CLASSDICT && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.classdict = list_add(fmd.classdict, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILELANGS && datatype == RPM_STRING_ARRAY_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                fmd.filelangs = list_add(fmd.filelangs, (char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_FILECOLORS && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filecolors = uint32_list_add(fmd.filecolors, ntohl(val32));
                p += sizeof(uint32_t);
            }
        }
    }

    /* No file list found */
    if (fmd.dirnames == NULL || fmd.basenames == NULL || fmd.dirindexes == NULL) {
        free_file_metadata(&fmd);
        return NULL;
    }

    /* Verify lists have consistent lengths */
    if (str_list_len(fmd.basenames) != uint32_list_len(fmd.dirindexes)) {
        warnx(_("*** file list arrays have mismatched lengths"));
        free_file_metadata(&fmd);
        return NULL;
    }

    /* Build the files array */
    files = json_object_new_array();
    ndirnames = str_list_len(fmd.dirnames);
    basename = first_str(fmd.basenames);
    username = first_str(fmd.fileusernames);
    groupname = first_str(fmd.filegroupnames);
    digest = first_str(fmd.filedigests);
    linkto = first_str(fmd.filelinktos);
    filelang = first_str(fmd.filelangs);
    dirindex = first_uint32(fmd.dirindexes);
    filesize = first_uint32(fmd.filesizes);
    filemode = first_uint32(fmd.filemodes);
    filemtime = first_uint32(fmd.filemtimes);
    filerdev = first_uint32(fmd.filerdevs);
    filedevice = first_uint32(fmd.filedevices);
    fileinode = first_uint32(fmd.fileinodes);
    fileclass = first_uint32(fmd.fileclass);
    filecolor = first_uint32(fmd.filecolors);

    while (basename != NULL && dirindex != NULL) {
        /* Verify dirindex is valid */
        dirname = str_list_nth(fmd.dirnames, dirindex->value);

        if (dirname == NULL) {
            warnx(_("*** invalid dirindex %u (max %u)"), dirindex->value, ndirnames - 1);
        } else {
            /* Combine dirname and basename */
            xasprintf(&path, "%s%s", dirname, basename->str);

            /* Create file entry with path */
            file = json_object_new_object();
            json_object_object_add(file, "path", json_object_new_string(path));

            /*
             * Add size for regular files and symlinks.  RPM stores the
             * length of a symlink's target string in FILESIZES, and the
             * payload reader consumes that many bytes for the target, so
             * the value must be preserved for symlinks too.
             */
            if (filesize != NULL && filemode != NULL) {
                if (S_ISREG(filemode->value) || S_ISLNK(filemode->value)) {
                    json_object_object_add(file, "size", json_object_new_int64(filesize->value));
                }
            }

            /* Add mode if available (as octal string of permission bits only) */
            if (filemode != NULL) {
                snprintf(mode_str, sizeof(mode_str), "%04o", filemode->value & ALLPERMS);
                json_object_object_add(file, "mode", json_object_new_string(mode_str));
            }

            /* Add mtime if available (as ISO 8601 timestamp) */
            if (filemtime != NULL) {
                mtime = (time_t) filemtime->value;
                tm_info = gmtime(&mtime);

                if (tm_info != NULL) {
                    strftime(mtime_str, sizeof(mtime_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                    json_object_object_add(file, "mtime", json_object_new_string(mtime_str));
                }
            }

            /* Add user if available */
            if (username != NULL) {
                json_object_object_add(file, "user", json_object_new_string(username->str));
            }

            /* Add group if available */
            if (groupname != NULL) {
                json_object_object_add(file, "group", json_object_new_string(groupname->str));
            }

            /* Add rdev if available (only for device nodes with non-zero values) */
            if (filerdev != NULL && filemode != NULL) {
                if ((S_ISCHR(filemode->value) || S_ISBLK(filemode->value)) && filerdev->value != 0) {
                    json_object_object_add(file, "rdev", json_object_new_int(filerdev->value));
                }
            }

            /* Add device if available */
            if (filedevice != NULL) {
                json_object_object_add(file, "device", json_object_new_int64(filedevice->value));
            }

            /*
             * Add digest for regular files that have one.  RPM stores an
             * empty string for entries without a digest (directories,
             * symlinks, device nodes, etc.), so only emit the key when the
             * digest is non-empty.
             */
            if (digest != NULL && digest->str[0] != '\0') {
                json_object_object_add(file, "digest", json_object_new_string(digest->str));
            }

            /*
             * Add linkto for symbolic links.  RPM stores an empty string
             * for entries that are not symlinks, so only emit the key when
             * the target is non-empty.
             */
            if (linkto != NULL && linkto->str[0] != '\0') {
                json_object_object_add(file, "linkto", json_object_new_string(linkto->str));
            }

            /*
             * Add inode for every entry.  RPM assigns these numbers itself
             * (starting at 1 and incrementing) to track hard links; files
             * that share an inode number are hard links of one another.
             */
            if (fileinode != NULL) {
                json_object_object_add(file, "inode", json_object_new_int(fileinode->value));
            }

            /*
             * Add class for entries that have one.  FILECLASS holds an index
             * into the CLASSDICT string array (libmagic-style type
             * descriptions), so resolve the index to its string here.  Skip
             * the key when the resolved class is an empty string.
             */
            if (fileclass != NULL) {
                classname = str_list_nth(fmd.classdict, fileclass->value);

                if (classname != NULL && classname[0] != '\0') {
                    json_object_object_add(file, "class", json_object_new_string(classname));
                }
            }

            /*
             * Add langs for entries that carry one.  RPM stores the
             * languages of a file as a single string with each language
             * separated by a "|", so split that in to an array here.
             * Entries with no language carry an empty string.
             */
            if (filelang != NULL && filelang->str[0] != '\0') {
                langs = strsplit(filelang->str, "|");

                if (langs != NULL) {
                    langs_array = json_object_new_array();

                    TAILQ_FOREACH(lang, langs, items) {
                        json_object_array_add(langs_array, json_object_new_string(lang->str));
                    }

                    json_object_object_add(file, "langs", langs_array);
                    list_free(langs, free);
                    langs = NULL;
                }
            }

            /*
             * Add colors for entries that carry one.  RPM records the
             * ELF class of a file as a bitfield, so turn that in to an
             * array of names here.  Entries with no color carry a zero,
             * which color_names() reports as NULL so the key is left off.
             */
            if (filecolor != NULL) {
                colors_array = color_names(filecolor->value);

                if (colors_array != NULL) {
                    json_object_object_add(file, "colors", colors_array);
                    colors_array = NULL;
                }
            }

            json_object_array_add(files, file);

            free(path);
            path = NULL;
        }

        basename = next_str(basename);
        username = next_str(username);
        groupname = next_str(groupname);
        digest = next_str(digest);
        linkto = next_str(linkto);
        filelang = next_str(filelang);
        dirindex = next_uint32(dirindex);
        filesize = next_uint32(filesize);
        filemode = next_uint32(filemode);
        filemtime = next_uint32(filemtime);
        filerdev = next_uint32(filerdev);
        filedevice = next_uint32(filedevice);
        fileinode = next_uint32(fileinode);
        fileclass = next_uint32(fileclass);
        filecolor = next_uint32(filecolor);
    }

    /* Cleanup */
    free_file_metadata(&fmd);

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
    struct json_object *langs_obj = NULL;
    struct json_object *colors_obj = NULL;
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
    struct json_object *filelangs = NULL;
    struct json_object *filecolors = NULL;
    struct json_object *tag = NULL;
    str_list_t *langs = NULL;
    char *langs_str = NULL;
    size_t nlangs = 0;
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
    uint32_t color = 0;
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
    filelangs = json_object_new_array();
    filecolors = json_object_new_array();

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

        /*
         * Extract langs if available.  The "langs" value is an array of
         * language strings which RPM stores as a single string with each
         * language separated by a "|".  Entries without a "langs" key
         * carry an empty string so the array stays parallel to the file
         * list.
         */
        langs_str = NULL;

        if (json_object_object_get_ex(file, "langs", &langs_obj) && json_object_get_type(langs_obj) == json_type_array) {
            nlangs = json_object_array_length(langs_obj);

            for (j = 0; j < nlangs; j++) {
                langs = list_add(langs, json_object_get_string(json_object_array_get_idx(langs_obj, j)));
            }

            langs_str = list_to_string(langs, "|");
            list_free(langs, free);
            langs = NULL;
        }

        if (langs_str != NULL) {
            json_object_array_add(filelangs, json_object_new_string(langs_str));
            free(langs_str);
            langs_str = NULL;
        } else {
            json_object_array_add(filelangs, json_object_new_string(""));
        }

        /*
         * Extract colors if available.  The "colors" value is an array
         * of color names which RPM stores as a bitfield.  Entries
         * without a "colors" key carry a zero so the array stays
         * parallel to the file list.
         */
        color = 0;

        if (json_object_object_get_ex(file, "colors", &colors_obj)) {
            color = color_value(colors_obj);
        }

        json_object_array_add(filecolors, json_object_new_int64(color));

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

    /* Add RPMTAG_FILELANGS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILELANGS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filelangs);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILECOLORS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILECOLORS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, filecolors);
    json_object_array_add(tags, tag);

    return;
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
        case RPMTAG_FILELANGS:
        case RPMTAG_FILECOLORS:
            return true;
    }

    return false;
}
