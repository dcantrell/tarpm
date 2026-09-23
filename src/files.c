/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <err.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmfc.h>
#include <rpm/rpmfiles.h>
#include <rpm/rpmpgp.h>
#include <openssl/evp.h>
#include <json.h>

#include "tarpm.h"

/*
 * The raw file lists we read out of the header to build the "files"
 * array.  Every list has one entry per file, in the order of the
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
    uint32_list_t *fileflags;
    uint32_list_t *fileverifyflags;
    uint32_list_t *filedependsx;
    uint32_list_t *filedependsn;

    /*
     * The one list here that is not per-file.  Every file shares
     * DEPENDSDICT.  FILEDEPENDSX and FILEDEPENDSN give the start and
     * the length of each file's slice of it.
     */
    uint32_list_t *dependsdict;
};

/*
 * Where we are in each of the file lists.  We walk all of them at the
 * same time, one step per file, so a cursor is NULL when that list is
 * missing or when we run off the end of it.
 */
struct file_cursors {
    str_entry_t *basename;
    str_entry_t *username;
    str_entry_t *groupname;
    str_entry_t *digest;
    str_entry_t *linkto;
    str_entry_t *filelang;
    uint32_entry_t *dirindex;
    uint32_entry_t *filesize;
    uint32_entry_t *filemode;
    uint32_entry_t *filemtime;
    uint32_entry_t *filerdev;
    uint32_entry_t *filedevice;
    uint32_entry_t *fileinode;
    uint32_entry_t *fileclass;
    uint32_entry_t *filecolor;
    uint32_entry_t *fileflag;
    uint32_entry_t *fileverifyflag;
    uint32_entry_t *filedependsx;
    uint32_entry_t *filedependsn;
};

/*
 * What a walk of the payload tree needs to add entries to the
 * "files" array.
 */
struct payload_scan {
    struct json_object *files;
    uint32_t digestalgo;
    bool source_package;
};

/*
 * Helper struct for add_file_list_tags().  One array per file list
 * tag, all of them parallel to the file list.  The rest tracks what
 * rpm stores once and has each file point at: the directory names,
 * the file classes and the hard link groups.
 */
struct file_list_tags {
    struct json_object *dirnames;
    struct json_object *basenames;
    struct json_object *dirindexes;
    struct json_object *filesizes;
    struct json_object *filemodes;
    struct json_object *filemtimes;
    struct json_object *fileusernames;
    struct json_object *filegroupnames;
    struct json_object *filerdevs;
    struct json_object *filedevices;
    struct json_object *filedigests;
    struct json_object *filelinktos;
    struct json_object *fileinodes;
    struct json_object *fileclass;
    struct json_object *classdict;
    struct json_object *filelangs;
    struct json_object *filecolors;
    struct json_object *fileflags;
    struct json_object *fileverifyflags;
    struct json_object *filedependsx;
    struct json_object *filedependsn;
    struct json_object *dependsdict;

    /* the directory names, in the order they first show up */
    char **dirs;
    size_t ndirs;

    /* the class strings, in the order they first show up */
    char **classes;
    size_t nclasses;

    /* what each hard link group carried and the number rpm gives it */
    int *inodes;
    int *mapped;
    size_t ninodes;
};

/*
 * What we carry along while we walk the "files" array.  The lists we
 * fill in, the things every file needs, and the two totals that grow
 * as we go.
 */
struct file_list_build {
    struct file_list_tags *out;
    struct json_object *dependencies;
    const char *payload_dir;
    uint32_t digestalgo;
    size_t nkept;
    int64_t totalsize;
};

/*
 * Maps a single RPMFILE_* bit to the name it carries in the "files"
 * array.  The names are the enum constant names from rpmfileAttrs_e
 * with the RPMFILE_ prefix trimmed and lowercased.
 */
struct file_flag_name {
    uint32_t bit;
    const char *name;
};

static const struct file_flag_name file_flag_names[] = {
    { RPMFILE_CONFIG, RPM_FILE_FLAG_CONFIG },
    { RPMFILE_DOC, RPM_FILE_FLAG_DOC },
    { RPMFILE_ICON, RPM_FILE_FLAG_ICON },
    { RPMFILE_MISSINGOK, RPM_FILE_FLAG_MISSINGOK },
    { RPMFILE_NOREPLACE, RPM_FILE_FLAG_NOREPLACE },
    { RPMFILE_SPECFILE, RPM_FILE_FLAG_SPECFILE },
    { RPMFILE_GHOST, RPM_FILE_FLAG_GHOST },
    { RPMFILE_LICENSE, RPM_FILE_FLAG_LICENSE },
    { RPMFILE_README, RPM_FILE_FLAG_README },
    { RPMFILE_PUBKEY, RPM_FILE_FLAG_PUBKEY },
    { RPMFILE_ARTIFACT, RPM_FILE_FLAG_ARTIFACT },
    { 0, NULL }
};

/*
 * Maps a single RPMVERIFY_* bit to the name it carries in the "files"
 * array.  The names come from the rpmVerifyAttrs_e constants with the
 * RPMVERIFY_ prefix cut off and lowercased, which is how a spec file
 * writes them without the '%'.
 *
 * Only bits 0 through 8 are here because those are the ones
 * %verify() takes.  rpm uses the higher bits itself and we never read
 * them back.  They are still in the stored value.  rpmbuild starts
 * every file at RPMVERIFY_ALL, which is ~0, so a file that verifies
 * everything carries 0xffffffff.
 *
 * RPMVERIFY_MD5 is left out because it is an old name for
 * RPMVERIFY_FILEDIGEST and shares the same bit.  We still take it
 * when reading.
 */
static const struct file_flag_name file_verify_names[] = {
    { RPMVERIFY_FILEDIGEST, RPM_FILE_VERIFY_FILEDIGEST },
    { RPMVERIFY_FILESIZE, RPM_FILE_VERIFY_FILESIZE },
    { RPMVERIFY_LINKTO, RPM_FILE_VERIFY_LINKTO },
    { RPMVERIFY_USER, RPM_FILE_VERIFY_USER },
    { RPMVERIFY_GROUP, RPM_FILE_VERIFY_GROUP },
    { RPMVERIFY_MTIME, RPM_FILE_VERIFY_MTIME },
    { RPMVERIFY_MODE, RPM_FILE_VERIFY_MODE },
    { RPMVERIFY_RDEV, RPM_FILE_VERIFY_RDEV },
    { RPMVERIFY_CAPS, RPM_FILE_VERIFY_CAPS },
    { 0, NULL }
};

/*
 * Maps the file type bits of a mode to the name they carry in the
 * "files" array.  The types are the ones rpmfiWhatis() in the rpm
 * source picks out of a mode.
 */
struct file_type_name {
    mode_t bits;
    const char *name;
};

static const struct file_type_name file_type_names[] = {
    { S_IFIFO, RPM_FILE_TYPE_PIPE },
    { S_IFCHR, RPM_FILE_TYPE_CHARDEV },
    { S_IFDIR, RPM_FILE_TYPE_DIR },
    { S_IFBLK, RPM_FILE_TYPE_BLOCKDEV },
    { S_IFREG, RPM_FILE_TYPE_FILE },
    { S_IFLNK, RPM_FILE_TYPE_SYMLINK },
    { S_IFSOCK, RPM_FILE_TYPE_SOCKET },
    { 0, NULL }
};

/*
 * Returns the name for the file type bits of a mode.  We let
 * rpmfiWhatis() pick the type so we read a mode the way rpm does.
 * Anything rpm does not know is a regular file.
 */
static const char *
type_name(rpm_mode_t mode)
{
    const char *name = RPM_FILE_TYPE_FILE;

    switch (rpmfiWhatis(mode)) {
        case PIPE:
            name = RPM_FILE_TYPE_PIPE;
            break;
        case CDEV:
            name = RPM_FILE_TYPE_CHARDEV;
            break;
        case XDIR:
            name = RPM_FILE_TYPE_DIR;
            break;
        case BDEV:
            name = RPM_FILE_TYPE_BLOCKDEV;
            break;
        case LINK:
            name = RPM_FILE_TYPE_SYMLINK;
            break;
        case SOCK:
            name = RPM_FILE_TYPE_SOCKET;
            break;
        default:
            break;
    }

    return name;
}

/*
 * Returns the file type bits a type name stands for.  An unknown name
 * or a NULL gives back 0 so the caller can fall back to what it knows
 * about the file.
 */
static mode_t
type_bits(const char *name)
{
    int i = 0;

    if (name == NULL) {
        return 0;
    }

    for (i = 0; file_type_names[i].name != NULL; i++) {
        if (!strcmp(file_type_names[i].name, name)) {
            return file_type_names[i].bits;
        }
    }

    warnx(_("*** unknown file type %s"), name);

    return 0;
}

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
    uint32_list_free(fmd->fileflags);
    uint32_list_free(fmd->fileverifyflags);
    uint32_list_free(fmd->filedependsx);
    uint32_list_free(fmd->filedependsn);
    uint32_list_free(fmd->dependsdict);
    return;
}

/*
 * Turn a FILECOLORS value in to an array of color names.  Bit 0 is a
 * 32-bit ELF object and bit 1 is a 64-bit one.  Bits with no name go
 * in as one decimal number so a round trip keeps the value.  Returns
 * NULL for a color of zero and the caller leaves the key off.
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
        json_object_array_add(names, json_object_new_string(RPM_FILE_COLOR_ELF32));
        rest &= ~((uint32_t) RPMFC_ELF32);
    }

    if (color & RPMFC_ELF64) {
        json_object_array_add(names, json_object_new_string(RPM_FILE_COLOR_ELF64));
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

        if (!strcmp(s, RPM_FILE_COLOR_ELF32)) {
            color |= RPMFC_ELF32;
        } else if (!strcmp(s, RPM_FILE_COLOR_ELF64)) {
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
 * Turn a FILEFLAGS value in to an array of flag names.  The names
 * come from the rpmfileAttrs_e constants with the RPMFILE_ prefix cut
 * off and lowercased.  Bits with no name go in as one decimal number
 * so a round trip keeps the value.  Returns NULL for flags of zero
 * and the caller leaves the key off.
 */
static struct json_object *
flag_names(uint32_t flags)
{
    uint32_t rest = 0;
    char *s = NULL;
    const struct file_flag_name *ffn = NULL;
    struct json_object *names = NULL;

    if (flags == 0) {
        return NULL;
    }

    names = json_object_new_array();
    rest = flags;

    for (ffn = file_flag_names; ffn->name != NULL; ffn++) {
        if (flags & ffn->bit) {
            json_object_array_add(names, json_object_new_string(ffn->name));
            rest &= ~(ffn->bit);
        }
    }

    if (rest != 0) {
        xasprintf(&s, "%u", rest);
        json_object_array_add(names, json_object_new_string(s));
        free(s);
    }

    return names;
}

/*
 * Convert an array of flag name strings back in to a FILEFLAGS value.
 * Names that flag_names() had no name for come back as a decimal
 * number.  Returns zero for a missing or empty array.
 */
static uint32_t
flag_value(struct json_object *names)
{
    size_t i = 0;
    size_t len = 0;
    uint32_t flags = 0;
    const char *s = NULL;
    char *end = NULL;
    unsigned long value = 0;
    const struct file_flag_name *ffn = NULL;
    bool found = false;

    if (names == NULL || json_object_get_type(names) != json_type_array) {
        return 0;
    }

    len = json_object_array_length(names);

    for (i = 0; i < len; i++) {
        s = json_object_get_string(json_object_array_get_idx(names, i));

        if (s == NULL) {
            continue;
        }

        found = false;

        for (ffn = file_flag_names; ffn->name != NULL; ffn++) {
            if (!strcmp(s, ffn->name)) {
                flags |= ffn->bit;
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        errno = 0;
        value = strtoul(s, &end, 10);

        if (errno != 0 || end == s || *end != '\0') {
            warnx(_("*** unknown file flag: %s"), s);
            continue;
        }

        flags |= (uint32_t) value;
    }

    return flags;
}

/*
 * Turn a FILEVERIFYFLAGS value in to an array of verify flag names.
 * The names come from the rpmVerifyAttrs_e constants with the
 * RPMVERIFY_ prefix cut off and lowercased.  We keep only the named
 * bits and drop the rest, so header.json lists what rpm really
 * checks.  Returns NULL when no named bit is set and the caller
 * leaves the key off.
 */
static struct json_object *
verifyflag_names(uint32_t verifyflags)
{
    const struct file_flag_name *ffn = NULL;
    struct json_object *names = NULL;

    for (ffn = file_verify_names; ffn->name != NULL; ffn++) {
        if (verifyflags & ffn->bit) {
            if (names == NULL) {
                names = json_object_new_array();
            }

            json_object_array_add(names, json_object_new_string(ffn->name));
        }
    }

    return names;
}

/*
 * Turn an array of verify flag names back in to a FILEVERIFYFLAGS
 * value.  We take the names verifyflag_names() writes plus "md5", the
 * old name for "filedigest".  rpmbuild starts at RPMVERIFY_ALL and
 * clears the bits the spec file skips, so we build the value the same
 * way: every bit outside the named ones stays set and the named bits
 * the array leaves out are cleared.  A missing or empty array
 * verifies none of the named attributes.
 */
static uint32_t
verifyflag_value(struct json_object *names)
{
    size_t i = 0;
    size_t len = 0;
    uint32_t verifyflags = 0;
    uint32_t named = 0;
    const char *s = NULL;
    const struct file_flag_name *ffn = NULL;
    bool found = false;

    for (ffn = file_verify_names; ffn->name != NULL; ffn++) {
        named |= ffn->bit;
    }

    if (names != NULL && json_object_get_type(names) == json_type_array) {
        len = json_object_array_length(names);
    }

    for (i = 0; i < len; i++) {
        s = json_object_get_string(json_object_array_get_idx(names, i));

        if (s == NULL) {
            continue;
        }

        /* the obsolete spelling of RPMVERIFY_FILEDIGEST */
        if (!strcmp(s, RPM_FILE_VERIFY_MD5)) {
            verifyflags |= RPMVERIFY_MD5;
            continue;
        }

        found = false;

        for (ffn = file_verify_names; ffn->name != NULL; ffn++) {
            if (!strcmp(s, ffn->name)) {
                verifyflags |= ffn->bit;
                found = true;
                break;
            }
        }

        if (!found) {
            warnx(_("*** unknown file verify flag: %s"), s);
        }
    }

    return verifyflags | ~named;
}

/*
 * Turn one depends dictionary value in to a copy of the dependency it
 * names.  The high byte gives us the type and the rest is the index
 * in to that type's array in "dependencies".  We name the array the
 * dependency came from first and then copy the rest of it.  Returns
 * NULL when we cannot tell what the value points at.
 */
static struct json_object *
dependency_ref(struct json_object *dependencies, const uint32_t value)
{
    char abbrev = '\0';
    uint32_t index = 0;
    const char *type = NULL;
    struct json_object *deps = NULL;
    struct json_object *entry = NULL;
    struct json_object *ref = NULL;

    if (dependencies == NULL) {
        return NULL;
    }

    abbrev = (char) ((value >> DEPENDS_DICT_TYPE_SHIFT) & 0xFF);
    index = value & DEPENDS_DICT_INDEX_MASK;
    type = dependency_type_key(abbrev);

    if (type == NULL) {
        warnx(_("*** unknown dependency type '%c' in the depends dictionary"), abbrev);
        return NULL;
    }

    if (!json_object_object_get_ex(dependencies, type, &deps)) {
        warnx(_("*** no %s dependencies for the depends dictionary"), type);
        return NULL;
    }

    entry = json_object_array_get_idx(deps, index);

    if (entry == NULL) {
        warnx(_("*** %s dependency %u is out of range"), type, index);
        return NULL;
    }

    /* copy the dependency, naming the array it came from first */
    ref = json_object_new_object();
    json_object_object_add(ref, RPM_DEPENDENCY_TYPE_DESC, json_object_new_string(type));

    json_object_object_foreach(entry, depkey, depval) {
        json_object_object_add(ref, depkey, json_object_get(depval));
    }

    return ref;
}

/*
 * Turn one file's slice of the depends dictionary in to an array of
 * the dependencies that file made.  We keep the order of the slice
 * because rpm lists these in the order the generators made them, so
 * the types can mix and the same dependency can show up twice.
 * Create mode needs that order back.
 *
 * Returns NULL when the slice is empty or when nothing in it works
 * out, and then the caller leaves the key off.
 */
static struct json_object *
file_dependencies(struct json_object *dependencies, const uint32_list_t *dependsdict, uint32_t start, uint32_t count)
{
    uint32_t i = 0;
    uint32_t value = 0;
    struct json_object *ref = NULL;
    struct json_object *refs = NULL;

    if (dependencies == NULL || dependsdict == NULL) {
        return NULL;
    }

    for (i = 0; i < count; i++) {
        if (!uint32_list_nth(dependsdict, start + i, &value)) {
            warnx(_("*** depends dictionary index %u is out of range"), start + i);
            continue;
        }

        ref = dependency_ref(dependencies, value);

        if (ref == NULL) {
            continue;
        }

        if (refs == NULL) {
            refs = json_object_new_array();
        }

        json_object_array_add(refs, ref);
    }

    return refs;
}

/*
 * Read count strings out of the header data and add them to the list.
 * A header can carry the same tag more than once, so we add to what
 * is there rather than start over.
 */
static str_list_t *
read_str_list(str_list_t *list, const uint8_t *data, const uint32_t count)
{
    uint32_t i = 0;
    const uint8_t *p = data;

    for (i = 0; i < count; i++) {
        list = list_add(list, (const char *) p);
        p += strlen((const char *) p) + 1;
    }

    return list;
}

/*
 * Read count 32 bit numbers out of the header data and add them to
 * the list in host order.
 */
static uint32_list_t *
read_uint32_list(uint32_list_t *list, const uint8_t *data, const uint32_t count)
{
    uint32_t i = 0;
    uint32_t value = 0;
    const uint8_t *p = data;

    for (i = 0; i < count; i++) {
        memcpy(&value, p, sizeof(value));
        list = uint32_list_add(list, ntohl(value));
        p += sizeof(value);
    }

    return list;
}

/*
 * Read count 16 bit numbers out of the header data and add them to
 * the list in host order.
 */
static uint32_list_t *
read_uint16_list(uint32_list_t *list, const uint8_t *data, const uint32_t count)
{
    uint32_t i = 0;
    uint16_t value = 0;
    const uint8_t *p = data;

    for (i = 0; i < count; i++) {
        memcpy(&value, p, sizeof(value));
        list = uint32_list_add(list, ntohs(value));
        p += sizeof(value);
    }

    return list;
}

/*
 * Read the file lists we need out of the header data.  Tags the
 * header does not carry leave their list NULL.
 */
static void
collect_file_metadata(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct file_metadata *fmd)
{
    uint32_t i = 0;
    uint32_t tag = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    rpmTagType datatype = 0;
    uint8_t *data = NULL;
    struct rpmhdrentry *hdrentry = NULL;

    if (hdr == NULL || hdrinfo == NULL || fmd == NULL) {
        return;
    }

    hdrentry = hdrinfo->estart;

    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);
        data = hdrinfo->datastart + offset;

        if (tag == RPMTAG_DIRNAMES && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->dirnames = read_str_list(fmd->dirnames, data, count);
        } else if (tag == RPMTAG_BASENAMES && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->basenames = read_str_list(fmd->basenames, data, count);
        } else if (tag == RPMTAG_DIRINDEXES && datatype == RPM_INT32_TYPE) {
            fmd->dirindexes = read_uint32_list(fmd->dirindexes, data, count);
        } else if (tag == RPMTAG_FILESIZES && datatype == RPM_INT32_TYPE) {
            fmd->filesizes = read_uint32_list(fmd->filesizes, data, count);
        } else if (tag == RPMTAG_FILEMODES && datatype == RPM_INT16_TYPE) {
            fmd->filemodes = read_uint16_list(fmd->filemodes, data, count);
        } else if (tag == RPMTAG_FILEMTIMES && datatype == RPM_INT32_TYPE) {
            fmd->filemtimes = read_uint32_list(fmd->filemtimes, data, count);
        } else if (tag == RPMTAG_FILEUSERNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->fileusernames = read_str_list(fmd->fileusernames, data, count);
        } else if (tag == RPMTAG_FILEGROUPNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->filegroupnames = read_str_list(fmd->filegroupnames, data, count);
        } else if (tag == RPMTAG_FILERDEVS && datatype == RPM_INT16_TYPE) {
            fmd->filerdevs = read_uint16_list(fmd->filerdevs, data, count);
        } else if (tag == RPMTAG_FILEDEVICES && datatype == RPM_INT32_TYPE) {
            fmd->filedevices = read_uint32_list(fmd->filedevices, data, count);
        } else if (tag == RPMTAG_FILEDIGESTS && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->filedigests = read_str_list(fmd->filedigests, data, count);
        } else if (tag == RPMTAG_FILELINKTOS && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->filelinktos = read_str_list(fmd->filelinktos, data, count);
        } else if (tag == RPMTAG_FILEINODES && datatype == RPM_INT32_TYPE) {
            fmd->fileinodes = read_uint32_list(fmd->fileinodes, data, count);
        } else if (tag == RPMTAG_FILECLASS && datatype == RPM_INT32_TYPE) {
            fmd->fileclass = read_uint32_list(fmd->fileclass, data, count);
        } else if (tag == RPMTAG_CLASSDICT && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->classdict = read_str_list(fmd->classdict, data, count);
        } else if (tag == RPMTAG_FILELANGS && datatype == RPM_STRING_ARRAY_TYPE) {
            fmd->filelangs = read_str_list(fmd->filelangs, data, count);
        } else if (tag == RPMTAG_FILECOLORS && datatype == RPM_INT32_TYPE) {
            fmd->filecolors = read_uint32_list(fmd->filecolors, data, count);
        } else if (tag == RPMTAG_FILEFLAGS && datatype == RPM_INT32_TYPE) {
            fmd->fileflags = read_uint32_list(fmd->fileflags, data, count);
        } else if (tag == RPMTAG_FILEVERIFYFLAGS && datatype == RPM_INT32_TYPE) {
            fmd->fileverifyflags = read_uint32_list(fmd->fileverifyflags, data, count);
        } else if (tag == RPMTAG_FILEDEPENDSX && datatype == RPM_INT32_TYPE) {
            fmd->filedependsx = read_uint32_list(fmd->filedependsx, data, count);
        } else if (tag == RPMTAG_FILEDEPENDSN && datatype == RPM_INT32_TYPE) {
            fmd->filedependsn = read_uint32_list(fmd->filedependsn, data, count);
        } else if (tag == RPMTAG_DEPENDSDICT && datatype == RPM_INT32_TYPE) {
            fmd->dependsdict = read_uint32_list(fmd->dependsdict, data, count);
        }
    }

    return;
}

/*
 * Point every cursor at the first entry of its list.
 */
static void
first_file_cursors(const struct file_metadata *fmd, struct file_cursors *fc)
{
    if (fmd == NULL || fc == NULL) {
        return;
    }

    fc->basename = first_str(fmd->basenames);
    fc->username = first_str(fmd->fileusernames);
    fc->groupname = first_str(fmd->filegroupnames);
    fc->digest = first_str(fmd->filedigests);
    fc->linkto = first_str(fmd->filelinktos);
    fc->filelang = first_str(fmd->filelangs);
    fc->dirindex = first_uint32(fmd->dirindexes);
    fc->filesize = first_uint32(fmd->filesizes);
    fc->filemode = first_uint32(fmd->filemodes);
    fc->filemtime = first_uint32(fmd->filemtimes);
    fc->filerdev = first_uint32(fmd->filerdevs);
    fc->filedevice = first_uint32(fmd->filedevices);
    fc->fileinode = first_uint32(fmd->fileinodes);
    fc->fileclass = first_uint32(fmd->fileclass);
    fc->filecolor = first_uint32(fmd->filecolors);
    fc->fileflag = first_uint32(fmd->fileflags);
    fc->fileverifyflag = first_uint32(fmd->fileverifyflags);
    fc->filedependsx = first_uint32(fmd->filedependsx);
    fc->filedependsn = first_uint32(fmd->filedependsn);

    return;
}

/*
 * Step every cursor on to the next file.
 */
static void
next_file_cursors(struct file_cursors *fc)
{
    if (fc == NULL) {
        return;
    }

    fc->basename = next_str(fc->basename);
    fc->username = next_str(fc->username);
    fc->groupname = next_str(fc->groupname);
    fc->digest = next_str(fc->digest);
    fc->linkto = next_str(fc->linkto);
    fc->filelang = next_str(fc->filelang);
    fc->dirindex = next_uint32(fc->dirindex);
    fc->filesize = next_uint32(fc->filesize);
    fc->filemode = next_uint32(fc->filemode);
    fc->filemtime = next_uint32(fc->filemtime);
    fc->filerdev = next_uint32(fc->filerdev);
    fc->filedevice = next_uint32(fc->filedevice);
    fc->fileinode = next_uint32(fc->fileinode);
    fc->fileclass = next_uint32(fc->fileclass);
    fc->filecolor = next_uint32(fc->filecolor);
    fc->fileflag = next_uint32(fc->fileflag);
    fc->fileverifyflag = next_uint32(fc->fileverifyflag);
    fc->filedependsx = next_uint32(fc->filedependsx);
    fc->filedependsn = next_uint32(fc->filedependsn);

    return;
}

/*
 * Build the JSON entry for the file the cursors point at.  The keys
 * we have nothing to say about are left off.  The caller frees what
 * comes back by way of the files array it goes in to.
 */
static struct json_object *
file_entry(const struct file_metadata *fmd, const struct file_cursors *fc, struct json_object *dependencies, const char *path)
{
    char mode_str[5];
    char mtime_str[32];
    time_t mtime = 0;
    struct tm *tm_info = NULL;
    const char *classname = NULL;
    str_entry_t *lang = NULL;
    str_list_t *langs = NULL;
    struct json_object *file = NULL;
    struct json_object *langs_array = NULL;
    struct json_object *colors_array = NULL;
    struct json_object *flags_array = NULL;
    struct json_object *verifyflags_array = NULL;
    struct json_object *provides_array = NULL;

    if (fmd == NULL || fc == NULL || path == NULL) {
        return NULL;
    }

    /* Create file entry with path */
    file = json_object_new_object();
    json_object_object_add(file, RPM_FILE_PATH_DESC, json_object_new_string(path));

    /*
     * Add size for regular files and symlinks.  RPM keeps the length
     * of a symlink target in FILESIZES and the payload reader eats
     * that many bytes, so symlinks need it too.
     */
    if (fc->filesize != NULL && fc->filemode != NULL) {
        if (S_ISREG(fc->filemode->value) || S_ISLNK(fc->filemode->value)) {
            json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64(fc->filesize->value));
        }
    }

    /*
     * Add mode if available (as octal string of permission bits
     * only).  The file type bits of the mode go alongside it under
     * their own name.
     */
    if (fc->filemode != NULL) {
        snprintf(mode_str, sizeof(mode_str), RPM_FILE_MODE_FORMAT, fc->filemode->value & ALLPERMS);
        json_object_object_add(file, RPM_FILE_MODE_DESC, json_object_new_string(mode_str));
        json_object_object_add(file, RPM_FILE_TYPE_DESC, json_object_new_string(type_name(fc->filemode->value)));
    }

    /* Add mtime if available (as ISO 8601 timestamp) */
    if (fc->filemtime != NULL) {
        mtime = (time_t) fc->filemtime->value;
        tm_info = gmtime(&mtime);

        if (tm_info != NULL) {
            strftime(mtime_str, sizeof(mtime_str), RPM_FILE_MTIME_FORMAT, tm_info);
            json_object_object_add(file, RPM_FILE_MTIME_DESC, json_object_new_string(mtime_str));
        }
    }

    /* Add user if available */
    if (fc->username != NULL) {
        json_object_object_add(file, RPM_FILE_USER_DESC, json_object_new_string(fc->username->str));
    }

    /* Add group if available */
    if (fc->groupname != NULL) {
        json_object_object_add(file, RPM_FILE_GROUP_DESC, json_object_new_string(fc->groupname->str));
    }

    /* Add rdev if available (only for device nodes with non-zero values) */
    if (fc->filerdev != NULL && fc->filemode != NULL) {
        if ((S_ISCHR(fc->filemode->value) || S_ISBLK(fc->filemode->value)) && fc->filerdev->value != 0) {
            json_object_object_add(file, RPM_FILE_RDEV_DESC, json_object_new_int(fc->filerdev->value));
        }
    }

    /* Add device if available */
    if (fc->filedevice != NULL) {
        json_object_object_add(file, RPM_FILE_DEVICE_DESC, json_object_new_int64(fc->filedevice->value));
    }

    /*
     * Add digest for regular files that have one.  RPM stores an
     * empty string for entries without a digest (directories,
     * symlinks, device nodes, etc.), so only emit the key when the
     * digest is non-empty.
     */
    if (fc->digest != NULL && fc->digest->str[0] != '\0') {
        json_object_object_add(file, RPM_FILE_DIGEST_DESC, json_object_new_string(fc->digest->str));
    }

    /*
     * Add linkto for symbolic links.  RPM stores an empty string for
     * entries that are not symlinks, so only emit the key when the
     * target is non-empty.
     */
    if (fc->linkto != NULL && fc->linkto->str[0] != '\0') {
        json_object_object_add(file, RPM_FILE_LINKTO_DESC, json_object_new_string(fc->linkto->str));
    }

    /*
     * Add inode for every entry.  RPM numbers these itself starting
     * at 1 to track hard links.  Files that share a number are hard
     * links of one another.
     */
    if (fc->fileinode != NULL) {
        json_object_object_add(file, RPM_FILE_INODE_DESC, json_object_new_int(fc->fileinode->value));
    }

    /*
     * Add class for entries that have one.  FILECLASS holds an index
     * into the CLASSDICT string array (libmagic-style type
     * descriptions), so resolve the index to its string here.  Skip
     * the key when the resolved class is an empty string.
     */
    if (fc->fileclass != NULL) {
        classname = str_list_nth(fmd->classdict, fc->fileclass->value);

        if (classname != NULL && classname[0] != '\0') {
            json_object_object_add(file, RPM_FILE_CLASS_DESC, json_object_new_string(classname));
        }
    }

    /*
     * Add langs for entries that carry one.  RPM stores the languages
     * of a file as a single string with each language separated by a
     * "|", so split that in to an array here.  Entries with no
     * language carry an empty string.
     */
    if (fc->filelang != NULL && fc->filelang->str[0] != '\0') {
        langs = strsplit(fc->filelang->str, RPM_FILE_LANG_SEPARATOR);

        if (langs != NULL) {
            langs_array = json_object_new_array();

            TAILQ_FOREACH(lang, langs, items) {
                json_object_array_add(langs_array, json_object_new_string(lang->str));
            }

            json_object_object_add(file, RPM_FILE_LANGS_DESC, langs_array);
            list_free(langs, free);
        }
    }

    /*
     * Add colors for entries that carry one.  RPM records the ELF
     * class of a file as a bitfield, so turn that in to an array of
     * names here.  Entries with no color carry a zero, which
     * color_names() reports as NULL so the key is left off.
     */
    if (fc->filecolor != NULL) {
        colors_array = color_names(fc->filecolor->value);

        if (colors_array != NULL) {
            json_object_object_add(file, RPM_FILE_COLORS_DESC, colors_array);
        }
    }

    /*
     * Add flags for entries that carry one.  RPM records the %config,
     * %doc, %ghost and similar markings of a file as a bitfield, so
     * turn that in to an array of names here.  Entries with no flags
     * carry a zero, which flag_names() reports as NULL so the key is
     * left off.
     */
    if (fc->fileflag != NULL) {
        flags_array = flag_names(fc->fileflag->value);

        if (flags_array != NULL) {
            json_object_object_add(file, RPM_FILE_FLAGS_DESC, flags_array);
        }
    }

    /*
     * Add verifyflags for entries that carry one.  RPM records the
     * %verify() settings of a file as a bitfield, so turn that in to
     * an array of names here.  Entries that verify nothing carry a
     * zero, which verifyflag_names() reports as NULL so the key is
     * left off.
     */
    if (fc->fileverifyflag != NULL) {
        verifyflags_array = verifyflag_names(fc->fileverifyflag->value);

        if (verifyflags_array != NULL) {
            json_object_object_add(file, RPM_FILE_VERIFYFLAGS_DESC, verifyflags_array);
        }
    }

    /*
     * Add provides for entries that generated dependencies.
     * FILEDEPENDSX and FILEDEPENDSN hold the start and the length of
     * this file's slice of the depends dictionary, so resolve that
     * slice to the dependencies it names here.  Entries that
     * generated nothing carry a length of zero, so the key is left
     * off for them.
     */
    if (fc->filedependsx != NULL && fc->filedependsn != NULL && fc->filedependsn->value > 0) {
        provides_array = file_dependencies(dependencies, fmd->dependsdict, fc->filedependsx->value, fc->filedependsn->value);

        if (provides_array != NULL) {
            json_object_object_add(file, RPM_FILE_PROVIDES_DESC, provides_array);
        }
    }

    return file;
}

/*
 * Generate a "files" array from the DIRNAMES, BASENAMES, and DIRINDEXES tags.
 * Returns a JSON array where each entry is {"path": "/full/path/to/file"} and
 * optionally "size" for regular files.
 * Returns NULL if the required tags are not found.
 */
struct json_object *
generate_files(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct json_object *dependencies)
{
    uint32_t ndirnames = 0;
    char *path = NULL;
    const char *dirname = NULL;
    struct file_metadata fmd;
    struct file_cursors fc;
    struct json_object *files = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    /* start with empty lists and cursors */
    memset(&fmd, '\0', sizeof(fmd));
    memset(&fc, '\0', sizeof(fc));

    /* read the file lists out of the header */
    collect_file_metadata(hdr, hdrinfo, &fmd);

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
    first_file_cursors(&fmd, &fc);

    while (fc.basename != NULL && fc.dirindex != NULL) {
        /* Verify dirindex is valid */
        dirname = str_list_nth(fmd.dirnames, fc.dirindex->value);

        if (dirname == NULL) {
            warnx(_("*** invalid dirindex %u (max %u)"), fc.dirindex->value, ndirnames - 1);
        } else {
            /* Combine dirname and basename */
            xasprintf(&path, "%s%s", dirname, fc.basename->str);

            json_object_array_add(files, file_entry(&fmd, &fc, dependencies, path));

            free(path);
            path = NULL;
        }

        next_file_cursors(&fc);
    }

    /* Cleanup */
    free_file_metadata(&fmd);

    return files;
}

/*
 * Compute the digest of a file in the package payload tree the same
 * way RPM does.  Use the digest algorithm defined in
 * RPMTAG_FILEDIGESTALGO, but fall back to MD5 like rpm does.  Returns
 * NULL if the file cannot be read or if the algorithm is not one
 * OpenSSL provides.  Caller must free the string returned.
 */
static char *
mkfiledigest(const char *path, const uint32_t algo)
{
    unsigned int i = 0;
    size_t len = 0;
    unsigned int digest_sz = 0;
    char *name = NULL;
    char *r = NULL;
    unsigned char buf[BUFSIZ];
    unsigned char digest[EVP_MAX_MD_SIZE];
    const EVP_MD *md = NULL;
    EVP_MD_CTX *ctx = NULL;
    FILE *fp = NULL;

    if (path == NULL) {
        return NULL;
    }

    /* the algorithm names tarpm uses are the ones OpenSSL knows */
    name = strdigestalgo(algo);
    md = EVP_get_digestbyname(name);

    if (md == NULL) {
        warnx(_("*** unsupported file digest algorithm: %s"), name);
        free(name);
        return NULL;
    }

    free(name);
    fp = fopen(path, "rb");

    if (fp == NULL) {
        warn("fopen");
        return NULL;
    }

    ctx = EVP_MD_CTX_new();

    if (ctx == NULL) {
        warn("EVP_MD_CTX_new");
        goto cleanup_mkfiledigest;
    }

    if (EVP_DigestInit(ctx, md) == 0) {
        warn("EVP_DigestInit");
        goto cleanup_mkfiledigest;
    }

    len = fread(buf, 1, sizeof(buf), fp);

    while (len > 0) {
        if (EVP_DigestUpdate(ctx, buf, len) == 0) {
            warn("EVP_DigestUpdate");
            goto cleanup_mkfiledigest;
        }

        len = fread(buf, 1, sizeof(buf), fp);
    }

    if (ferror(fp)) {
        warn("fread");
        goto cleanup_mkfiledigest;
    }

    if (EVP_DigestFinal(ctx, digest, &digest_sz) == 0) {
        warn("EVP_DigestFinal");
        goto cleanup_mkfiledigest;
    }

    r = xalloc((digest_sz * 2) + 1);

    for (i = 0; i < digest_sz; i++) {
        snprintf(r + (i * 2), 3, "%02x", digest[i]);
    }

cleanup_mkfiledigest:
    EVP_MD_CTX_free(ctx);
    fclose(fp);
    return r;
}

/*
 * Returns true if the file an entry in the "files" array names is
 * gone from the payload tree.  %ghost files are never in the payload,
 * so we always report those as present.  Anything else that is not
 * there was taken out of the unpacked package and stays out of the
 * header.  With no payload tree to look in, every entry is there.
 */
static bool
missing_from_payload(struct json_object *file, const char *path, const char *payload_dir)
{
    char *file_path = NULL;
    bool missing = false;
    struct json_object *flags = NULL;
    struct stat sb;

    if (file == NULL || path == NULL || payload_dir == NULL) {
        return false;
    }

    if (json_object_object_get_ex(file, RPM_FILE_FLAGS_DESC, &flags) && (flag_value(flags) & RPMFILE_GHOST)) {
        return false;
    }

    file_path = joinpath(payload_dir, (path[0] == '/') ? path + 1 : path, NULL);
    missing = (lstat(file_path, &sb) != 0);
    free(file_path);

    return missing;
}

/*
 * Returns true if the "files" array needs an entry for this file in
 * the payload tree.  Paths the list already names do not need one.
 * Neither do the directories leading to a file the list names.  The
 * payload tree needs those to hold the files under them, but the
 * package never owned them.
 */
static bool
new_in_payload(struct json_object *files, const char *path, const struct stat *sb)
{
    size_t i = 0;
    size_t len = 0;
    size_t count = 0;
    const char *s = NULL;
    struct json_object *file = NULL;
    struct json_object *entry_path = NULL;

    count = json_object_array_length(files);
    len = strlen(path);

    for (i = 0; i < count; i++) {
        file = json_object_array_get_idx(files, i);

        if (!json_object_object_get_ex(file, RPM_FILE_PATH_DESC, &entry_path)) {
            continue;
        }

        s = json_object_get_string(entry_path);

        /* the list already names it */
        if (!strcmp(s, path)) {
            return false;
        }

        /* a directory on the way to something the list names */
        if (S_ISDIR(sb->st_mode) && !strncmp(s, path, len) && s[len] == '/') {
            return false;
        }
    }

    return true;
}

/*
 * Build the "files" array entry for a file in the payload tree.  The
 * keys are the ones generate_files() writes for a file of this type so
 * an added file reads back the same way an unpacked one does.  The
 * path is where the file lands when the package is installed and
 * file_path is where it sits in the payload tree now.
 */
static struct json_object *
mkfileentry(const struct payload_scan *scan, const char *path, const char *file_path, const struct stat *sb)
{
    ssize_t len = 0;
    char *target = NULL;
    char *digest = NULL;
    struct json_object *file = NULL;
    struct json_object *verifyflags = NULL;
    char mode_str[5];
    char mtime_str[32];
    time_t mtime = 0;
    struct tm *tm_info = NULL;

    file = json_object_new_object();
    json_object_object_add(file, RPM_FILE_PATH_DESC, json_object_new_string(path));

    /* a regular file carries the number of bytes it holds */
    if (S_ISREG(sb->st_mode)) {
        json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) sb->st_size));
    }

    /* a symlink carries its target and the length of it */
    if (S_ISLNK(sb->st_mode)) {
        target = xalloc(sb->st_size + 1);
        len = readlink(file_path, target, sb->st_size);

        if (len < 0) {
            warn("readlink");
            len = 0;
        }

        target[len] = '\0';
        json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) len));
        json_object_object_add(file, RPM_FILE_LINKTO_DESC, json_object_new_string(target));
        free(target);
    }

    /* the permission bits as an octal string and the type by name */
    snprintf(mode_str, sizeof(mode_str), RPM_FILE_MODE_FORMAT, sb->st_mode & ALLPERMS);
    json_object_object_add(file, RPM_FILE_MODE_DESC, json_object_new_string(mode_str));
    json_object_object_add(file, RPM_FILE_TYPE_DESC, json_object_new_string(type_name(sb->st_mode)));

    /* the mtime as an ISO 8601 timestamp */
    mtime = sb->st_mtime;
    tm_info = gmtime(&mtime);

    if (tm_info != NULL) {
        strftime(mtime_str, sizeof(mtime_str), RPM_FILE_MTIME_FORMAT, tm_info);
        json_object_object_add(file, RPM_FILE_MTIME_DESC, json_object_new_string(mtime_str));
    }

    /*
     * Who owns the file now is whoever ran tarpm, which is not who
     * should own it once the package is installed, so added files
     * get the default owner.
     */
    json_object_object_add(file, RPM_FILE_USER_DESC, json_object_new_string(RPM_FILE_DEFAULT_USER));
    json_object_object_add(file, RPM_FILE_GROUP_DESC, json_object_new_string(RPM_FILE_DEFAULT_GROUP));

    /* a device node carries the device number it names */
    if (S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode)) {
        json_object_object_add(file, RPM_FILE_RDEV_DESC, json_object_new_int((int) sb->st_rdev));
    }

    json_object_object_add(file, RPM_FILE_DEVICE_DESC, json_object_new_int64(RPM_FILE_DEFAULT_DEVICE));

    /* only regular files carry a digest */
    if (S_ISREG(sb->st_mode) && scan->digestalgo != 0) {
        digest = mkfiledigest(file_path, scan->digestalgo);

        if (digest != NULL) {
            json_object_object_add(file, RPM_FILE_DIGEST_DESC, json_object_new_string(digest));
            free(digest);
        }
    }

    /*
     * Hard links of one another share an inode number, which is all
     * the "inode" key is read for.  A file with one link does not
     * need it.
     */
    if (sb->st_nlink > 1) {
        json_object_object_add(file, RPM_FILE_INODE_DESC, json_object_new_int((int) sb->st_ino));
    }

    /* rpmbuild starts every file out verifying all of its attributes */
    verifyflags = verifyflag_names(RPMVERIFY_ALL);

    if (verifyflags != NULL) {
        json_object_object_add(file, RPM_FILE_VERIFYFLAGS_DESC, verifyflags);
    }

    return file;
}

/*
 * Walk one directory of the payload tree and add an entry to the
 * "files" array for everything in it the file list does not name yet.
 * A directory is added before what it holds, which is the order the
 * payload wants them in.  dir_path is the directory to read and prefix
 * is the installed path that leads to it.
 */
static void
scan_payload_dir(const struct payload_scan *scan, const char *dir_path, const char *prefix)
{
    int i = 0;
    int n = 0;
    char *path = NULL;
    char *file_path = NULL;
    struct dirent **entries = NULL;
    struct stat sb;

    n = scandir(dir_path, &entries, NULL, alphasort);

    if (n < 0) {
        warn("scandir: %s", dir_path);
        return;
    }

    for (i = 0; i < n; i++) {
        if (!strcmp(entries[i]->d_name, ".") || !strcmp(entries[i]->d_name, "..")) {
            free(entries[i]);
            continue;
        }

        /* source RPMs keep bare filenames, binary ones full paths */
        if (scan->source_package && prefix[0] == '\0') {
            path = strdup(entries[i]->d_name);
        } else {
            xasprintf(&path, "%s/%s", prefix, entries[i]->d_name);
        }

        file_path = joinpath(dir_path, entries[i]->d_name, NULL);

        if (lstat(file_path, &sb) == -1) {
            warn("lstat");
        } else {
            if (new_in_payload(scan->files, path, &sb)) {
                warnx(_("*** %s is new in the payload, adding it to the file list"), path);
                json_object_array_add(scan->files, mkfileentry(scan, path, file_path, &sb));
            }

            if (S_ISDIR(sb.st_mode)) {
                scan_payload_dir(scan, file_path, path);
            }
        }

        free(file_path);
        free(path);
        free(entries[i]);
    }

    free(entries);
    return;
}

/*
 * Add entries to the "files" array for the files and directories added
 * to the payload tree.  Done when creating an RPM so a file put in the
 * payload lands in the file list of the new package.
 */
void
add_payload_files(struct json_object *tags, struct json_object *files, const char *payload_dir)
{
    const char *algo = NULL;
    struct payload_scan scan = { 0 };
    struct stat sb;

    if (tags == NULL || files == NULL || payload_dir == NULL) {
        return;
    }

    if (json_object_get_type(files) != json_type_array) {
        return;
    }

    if (lstat(payload_dir, &sb) == -1 || !S_ISDIR(sb.st_mode)) {
        return;
    }

    /*
     * The digests of the added files are computed with the algorithm
     * the header names, which rpm defaults to MD5.
     */
    algo = get_tag_value(tags, rpmTagGetName(RPMTAG_FILEDIGESTALGO));

    if (algo == NULL) {
        scan.digestalgo = PGPHASHALGO_MD5;
    } else {
        scan.digestalgo = digest_algo(algo);
    }

    scan.files = files;
    scan.source_package = false;

    if (get_tag_value(tags, rpmTagGetName(RPMTAG_SOURCEPACKAGE)) != NULL) {
        scan.source_package = true;
    }

    scan_payload_dir(&scan, payload_dir, "");

    return;
}

/*
 * Make the metadata in the JSON match the real files in the payload
 * subdirectory.  We check the size, the type, the linkto of a symlink
 * and the rdev.  When a payload member changes type, we fix up the
 * rest of its metadata to suit the new type.
 */
static void
fix_type_mismatch(struct json_object *file, const char *path, const char *file_path, const struct stat *sb, const uint32_t digestalgo)
{
    ssize_t len = 0;
    char *target = NULL;
    char *digest = NULL;
    struct json_object *type = NULL;
    const char *entry_type = NULL;
    const char *payload_type = NULL;

    if (!json_object_object_get_ex(file, RPM_FILE_TYPE_DESC, &type)) {
        return;
    }

    entry_type = json_object_get_string(type);
    payload_type = type_name(sb->st_mode);

    if (!strcmp(entry_type, payload_type)) {
        return;
    }

    warnx(_("*** %s is a %s in the payload and not a %s, going with the payload"), path, payload_type, entry_type);
    json_object_object_add(file, RPM_FILE_TYPE_DESC, json_object_new_string(payload_type));

    /*
     * These belong to the type the entry used to name, so drop them
     * and take what the payload has in their place.
     */
    json_object_object_del(file, RPM_FILE_SIZE_DESC);
    json_object_object_del(file, RPM_FILE_DIGEST_DESC);
    json_object_object_del(file, RPM_FILE_LINKTO_DESC);
    json_object_object_del(file, RPM_FILE_RDEV_DESC);

    /* a regular file carries the bytes it holds and a digest of them */
    if (S_ISREG(sb->st_mode)) {
        json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) sb->st_size));

        if (digestalgo != 0) {
            digest = mkfiledigest(file_path, digestalgo);

            if (digest != NULL) {
                json_object_object_add(file, RPM_FILE_DIGEST_DESC, json_object_new_string(digest));
                free(digest);
            }
        }
    }

    /* a symlink carries its target and the length of it */
    if (S_ISLNK(sb->st_mode)) {
        target = xalloc(sb->st_size + 1);
        len = readlink(file_path, target, sb->st_size);

        if (len < 0) {
            warn("readlink");
            len = 0;
        }

        target[len] = '\0';
        json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) len));
        json_object_object_add(file, RPM_FILE_LINKTO_DESC, json_object_new_string(target));
        free(target);
    }

    /* a device node carries the device number it names */
    if (S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode)) {
        json_object_object_add(file, RPM_FILE_RDEV_DESC, json_object_new_int((int) sb->st_rdev));
    }

    return;
}

/*
 * Tell the user a key was missing from a "files" entry and say where
 * the value tarpm put there came from.
 */
static void
report_missing(const char *path, const char *key, const bool from_payload)
{
    if (from_payload) {
        warnx(_("*** %s is missing the %s file metadata, taking it from the payload"), path, key);
    } else {
        warnx(_("*** %s is missing the %s file metadata, using the default"), path, key);
    }

    return;
}

/*
 * Add the keys a "files" entry is missing, reading them off the file
 * in the payload tree.  A hand edited header.json can be missing keys
 * the header needs.  The owner, the group and the device are not
 * things the file can answer, so those get the same defaults a new
 * payload file gets.  Only the keys generate_files() writes for a
 * file of this type are added.  The user is told about each one.
 */
static void
add_missing_metadata(struct json_object *file, const char *path, const char *file_path, const struct stat *sb, const uint32_t digestalgo)
{
    ssize_t len = 0;
    char *target = NULL;
    char *digest = NULL;
    char mode_str[5];
    char mtime_str[32];
    time_t mtime = 0;
    struct tm *tm_info = NULL;

    if (file == NULL || path == NULL || file_path == NULL || sb == NULL) {
        return;
    }

    /* a regular file carries the number of bytes it holds */
    if (S_ISREG(sb->st_mode) && !json_object_object_get_ex(file, RPM_FILE_SIZE_DESC, NULL)) {
        report_missing(path, RPM_FILE_SIZE_DESC, true);
        json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) sb->st_size));
    }

    /* a symlink carries its target and the length of it */
    if (S_ISLNK(sb->st_mode) && (!json_object_object_get_ex(file, RPM_FILE_SIZE_DESC, NULL) || !json_object_object_get_ex(file, RPM_FILE_LINKTO_DESC, NULL))) {
        target = xalloc(sb->st_size + 1);
        len = readlink(file_path, target, sb->st_size);

        if (len < 0) {
            warn("readlink");
            len = 0;
        }

        target[len] = '\0';

        if (!json_object_object_get_ex(file, RPM_FILE_SIZE_DESC, NULL)) {
            report_missing(path, RPM_FILE_SIZE_DESC, true);
            json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64((int64_t) len));
        }

        if (!json_object_object_get_ex(file, RPM_FILE_LINKTO_DESC, NULL)) {
            report_missing(path, RPM_FILE_LINKTO_DESC, true);
            json_object_object_add(file, RPM_FILE_LINKTO_DESC, json_object_new_string(target));
        }

        free(target);
    }

    /* the permission bits as an octal string */
    if (!json_object_object_get_ex(file, RPM_FILE_MODE_DESC, NULL)) {
        report_missing(path, RPM_FILE_MODE_DESC, true);
        snprintf(mode_str, sizeof(mode_str), RPM_FILE_MODE_FORMAT, sb->st_mode & ALLPERMS);
        json_object_object_add(file, RPM_FILE_MODE_DESC, json_object_new_string(mode_str));
    }

    /* the type by name */
    if (!json_object_object_get_ex(file, RPM_FILE_TYPE_DESC, NULL)) {
        report_missing(path, RPM_FILE_TYPE_DESC, true);
        json_object_object_add(file, RPM_FILE_TYPE_DESC, json_object_new_string(type_name(sb->st_mode)));
    }

    /* the mtime as an ISO 8601 timestamp */
    if (!json_object_object_get_ex(file, RPM_FILE_MTIME_DESC, NULL)) {
        mtime = sb->st_mtime;
        tm_info = gmtime(&mtime);

        if (tm_info != NULL) {
            report_missing(path, RPM_FILE_MTIME_DESC, true);
            strftime(mtime_str, sizeof(mtime_str), RPM_FILE_MTIME_FORMAT, tm_info);
            json_object_object_add(file, RPM_FILE_MTIME_DESC, json_object_new_string(mtime_str));
        }
    }

    /*
     * Who owns the file now is whoever ran tarpm, which is not who
     * should own it once the package is installed, so use the default.
     */
    if (!json_object_object_get_ex(file, RPM_FILE_USER_DESC, NULL)) {
        report_missing(path, RPM_FILE_USER_DESC, false);
        json_object_object_add(file, RPM_FILE_USER_DESC, json_object_new_string(RPM_FILE_DEFAULT_USER));
    }

    if (!json_object_object_get_ex(file, RPM_FILE_GROUP_DESC, NULL)) {
        report_missing(path, RPM_FILE_GROUP_DESC, false);
        json_object_object_add(file, RPM_FILE_GROUP_DESC, json_object_new_string(RPM_FILE_DEFAULT_GROUP));
    }

    /* a device node carries the device number it names */
    if ((S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode)) && !json_object_object_get_ex(file, RPM_FILE_RDEV_DESC, NULL)) {
        report_missing(path, RPM_FILE_RDEV_DESC, true);
        json_object_object_add(file, RPM_FILE_RDEV_DESC, json_object_new_int((int) sb->st_rdev));
    }

    /*
     * The device the file sits on now is not the one the package
     * records, so use the default.
     */
    if (!json_object_object_get_ex(file, RPM_FILE_DEVICE_DESC, NULL)) {
        report_missing(path, RPM_FILE_DEVICE_DESC, false);
        json_object_object_add(file, RPM_FILE_DEVICE_DESC, json_object_new_int64(RPM_FILE_DEFAULT_DEVICE));
    }

    /* only regular files carry a digest */
    if (S_ISREG(sb->st_mode) && digestalgo != 0 && !json_object_object_get_ex(file, RPM_FILE_DIGEST_DESC, NULL)) {
        digest = mkfiledigest(file_path, digestalgo);

        if (digest != NULL) {
            report_missing(path, RPM_FILE_DIGEST_DESC, true);
            json_object_object_add(file, RPM_FILE_DIGEST_DESC, json_object_new_string(digest));
            free(digest);
        }
    }

    /*
     * Hard links of one another share an inode number, which is all
     * the "inode" key is read for.  A file with one link does not
     * need it.
     */
    if (sb->st_nlink > 1 && !json_object_object_get_ex(file, RPM_FILE_INODE_DESC, NULL)) {
        report_missing(path, RPM_FILE_INODE_DESC, true);
        json_object_object_add(file, RPM_FILE_INODE_DESC, json_object_new_int((int) sb->st_ino));
    }

    return;
}

/*
 * Give back the value a JSON object carries for a key, or NULL when
 * the object does not have that key.
 */
static struct json_object *
key_object(struct json_object *obj, const char *key)
{
    struct json_object *value = NULL;

    if (obj == NULL || key == NULL) {
        return NULL;
    }

    if (!json_object_object_get_ex(obj, key, &value)) {
        return NULL;
    }

    return value;
}

/*
 * Give back the string a JSON object carries for a key, or dflt when
 * the object does not have that key.
 */
static const char *
key_string(struct json_object *obj, const char *key, const char *dflt)
{
    const char *str = NULL;
    struct json_object *value = NULL;

    value = key_object(obj, key);

    if (value == NULL) {
        return dflt;
    }

    str = json_object_get_string(value);

    return (str == NULL) ? dflt : str;
}

/*
 * Give back the number a JSON object carries for a key, or zero when
 * the object does not have that key.
 */
static int64_t
key_number(struct json_object *obj, const char *key)
{
    struct json_object *value = NULL;

    value = key_object(obj, key);

    if (value == NULL) {
        return 0;
    }

    return json_object_get_int64(value);
}

/*
 * Find where a string sits in the dictionary we are building.  One we
 * have not seen yet goes on the end.  rpm keeps the directory names
 * and the file classes this way and has each file point at one.
 */
static int
dict_index(char **strings, size_t *count, const char *str)
{
    size_t i = 0;

    for (i = 0; i < *count; i++) {
        if (!strcmp(strings[i], str)) {
            return (int) i;
        }
    }

    strings[*count] = strdup(str);
    *count += 1;

    return (int) (*count - 1);
}

/*
 * Add one array tag to the tags array.  The tag takes over the value,
 * so the caller does not free it.
 */
static void
add_tag_array(struct json_object *tags, const rpmTagVal tagnum, const rpmTagType type, struct json_object *value)
{
    struct json_object *tag = NULL;

    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tagnum)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, value);
    json_object_array_add(tags, tag);

    return;
}

/*
 * Set up the arrays we fill in as we walk the files.  The three we
 * track ourselves hold at most one entry per file.
 */
static void
new_file_list_arrays(struct file_list_tags *out, const size_t count)
{
    out->dirs = xcalloc(count, sizeof(char *));
    out->classes = xcalloc(count, sizeof(char *));
    out->inodes = xcalloc(count, sizeof(int));
    out->mapped = xcalloc(count, sizeof(int));

    out->dirnames = json_object_new_array();
    out->basenames = json_object_new_array();
    out->dirindexes = json_object_new_array();
    out->filesizes = json_object_new_array();
    out->filemodes = json_object_new_array();
    out->filemtimes = json_object_new_array();
    out->fileusernames = json_object_new_array();
    out->filegroupnames = json_object_new_array();
    out->filerdevs = json_object_new_array();
    out->filedevices = json_object_new_array();
    out->filedigests = json_object_new_array();
    out->filelinktos = json_object_new_array();
    out->fileinodes = json_object_new_array();
    out->fileclass = json_object_new_array();
    out->classdict = json_object_new_array();
    out->filelangs = json_object_new_array();
    out->filecolors = json_object_new_array();
    out->fileflags = json_object_new_array();
    out->fileverifyflags = json_object_new_array();
    out->filedependsx = json_object_new_array();
    out->filedependsn = json_object_new_array();
    out->dependsdict = json_object_new_array();

    return;
}

/*
 * Turn the directory names and the class strings we collected in to
 * their arrays and let go of what we tracked them with.
 */
static void
finish_file_list_arrays(struct file_list_tags *out)
{
    size_t i = 0;

    for (i = 0; i < out->ndirs; i++) {
        json_object_array_add(out->dirnames, json_object_new_string(out->dirs[i]));
        free(out->dirs[i]);
    }

    for (i = 0; i < out->nclasses; i++) {
        json_object_array_add(out->classdict, json_object_new_string(out->classes[i]));
        free(out->classes[i]);
    }

    free(out->dirs);
    free(out->classes);
    free(out->inodes);
    free(out->mapped);

    return;
}

/*
 * Split a path in to the directory name and the base name and add
 * them to the lists.  A path with no separator, which is how source
 * RPMs look, goes under an empty directory name.
 */
static void
add_file_path(struct file_list_tags *out, const char *path)
{
    char *dirname = NULL;
    const char *basename = NULL;
    const char *separator = NULL;

    separator = strrchr(path, '/');

    if (separator == NULL) {
        basename = path;
        dirname = strdup("");
    } else {
        basename = separator + 1;

        /* the directory name keeps its trailing slash */
        dirname = xalloc(separator - path + 2);
        memcpy(dirname, path, separator - path + 1);
        dirname[separator - path + 1] = '\0';
    }

    json_object_array_add(out->basenames, json_object_new_string(basename));
    json_object_array_add(out->dirindexes, json_object_new_int(dict_index(out->dirs, &out->ndirs, dirname)));

    free(dirname);

    return;
}

/*
 * Work out the size to store for a file.  Only entries that carry a
 * size get one.  A regular file in the payload tree wins so an edited
 * payload lands the right number in the header.
 */
static int64_t
file_size_value(struct json_object *file, const struct stat *sb)
{
    if (key_object(file, RPM_FILE_SIZE_DESC) == NULL) {
        return 0;
    }

    if (sb != NULL && S_ISREG(sb->st_mode)) {
        return (int64_t) sb->st_size;
    }

    return key_number(file, RPM_FILE_SIZE_DESC);
}

/*
 * Work out the mode to store for a file.  The permission bits come
 * from the entry and the type bits from the file in the payload tree.
 */
static int
file_mode_value(struct json_object *file, const struct stat *sb)
{
    int perms = 0;
    mode_t typebits = 0;
    const char *str = NULL;

    str = key_string(file, RPM_FILE_MODE_DESC, NULL);

    if (str == NULL) {
        return 0;
    }

    /* the mode in the entry is an octal string of the permission bits */
    perms = (int) strtol(str, NULL, 8);

    /* the file in the payload tree says what type it is */
    if (sb != NULL) {
        typebits = sb->st_mode & S_IFMT;
    }

    /*
     * Nothing in the payload to look at, so go by the type the file
     * list names.  %ghost entries land here.
     */
    if (typebits == 0) {
        str = key_string(file, RPM_FILE_TYPE_DESC, NULL);

        if (str != NULL) {
            typebits = type_bits(str);
        }
    }

    /*
     * A file list from an older tarpm names no type, so fall back to
     * the guess it used to make: an entry with a size is a regular
     * file and anything else is a directory.
     */
    if (typebits == 0) {
        if (key_object(file, RPM_FILE_SIZE_DESC) != NULL) {
            typebits = S_IFREG;
        } else {
            typebits = S_IFDIR;
        }
    }

    return typebits | (perms & ALLPERMS);
}

/*
 * Turn the ISO 8601 timestamp a file entry carries in to seconds.
 * Entries with no timestamp and ones we cannot read get a zero.
 */
static int64_t
file_mtime_value(struct json_object *file)
{
    struct tm tm_info;
    const char *str = NULL;

    str = key_string(file, RPM_FILE_MTIME_DESC, NULL);

    if (str == NULL) {
        return 0;
    }

    memset(&tm_info, 0, sizeof(tm_info));

    if (strptime(str, RPM_FILE_MTIME_FORMAT, &tm_info) == NULL) {
        return 0;
    }

    return (int64_t) timegm(&tm_info);
}

/*
 * Work out the digest to store for a file.  We read it back out of
 * the payload so an edited file lands a correct one in the header.
 * With no regular file there we keep what header.json had.  Entries
 * with no digest (directories, symlinks, device nodes) carry an empty
 * string so the array stays parallel to the file list.  The caller
 * has to free what comes back.
 */
static char *
file_digest_value(struct json_object *file, const char *file_path, const struct stat *sb, const uint32_t digestalgo)
{
    char *digest = NULL;
    const char *str = NULL;

    str = key_string(file, RPM_FILE_DIGEST_DESC, "");

    if (str[0] != '\0' && digestalgo != 0 && sb != NULL && S_ISREG(sb->st_mode)) {
        digest = mkfiledigest(file_path, digestalgo);

        if (digest != NULL) {
            return digest;
        }
    }

    return strdup(str);
}

/*
 * Join the languages a file entry carries in to the single separated
 * string rpm stores.  Entries with no languages get an empty string.
 * The caller has to free what comes back.
 */
static char *
file_langs_value(struct json_object *file)
{
    size_t i = 0;
    size_t nlangs = 0;
    char *joined = NULL;
    str_list_t *langs = NULL;
    struct json_object *value = NULL;

    value = key_object(file, RPM_FILE_LANGS_DESC);

    if (value == NULL || json_object_get_type(value) != json_type_array) {
        return strdup("");
    }

    nlangs = json_object_array_length(value);

    for (i = 0; i < nlangs; i++) {
        langs = list_add(langs, json_object_get_string(json_object_array_get_idx(value, i)));
    }

    joined = list_to_string(langs, RPM_FILE_LANG_SEPARATOR);
    list_free(langs, free);

    return (joined == NULL) ? strdup("") : joined;
}

/*
 * Work out the inode number to store for a file.  rpm numbers these
 * itself: an entry gets its place in the file list counting from one,
 * and hard links of one another all get the number the first one got.
 * We number them here instead of keeping what header.json had, so the
 * numbers stay right when entries drop out.  That leaves the "inode"
 * key as only a way to tell which entries were hard links.  Sets
 * shared when the entry joins a group we already saw.
 */
static int
file_inode_value(struct file_list_tags *out, struct json_object *file, const int next, bool *shared)
{
    size_t i = 0;
    int inode = 0;

    *shared = false;

    if (key_object(file, RPM_FILE_INODE_DESC) == NULL) {
        return next;
    }

    inode = (int) key_number(file, RPM_FILE_INODE_DESC);

    for (i = 0; i < out->ninodes; i++) {
        if (out->inodes[i] == inode) {
            *shared = true;
            return out->mapped[i];
        }
    }

    /* the first entry of a hard link group names the group */
    out->inodes[out->ninodes] = inode;
    out->mapped[out->ninodes] = next;
    out->ninodes++;

    return next;
}

/*
 * Add the dependencies a file generated to the depends dictionary and
 * give back how many went in.  Each one becomes a single value naming
 * its type and its place in the matching "dependencies" array.
 */
static size_t
add_file_provides(struct file_list_build *build, struct json_object *file, const char *path)
{
    size_t i = 0;
    size_t nprovides = 0;
    size_t ndepends = 0;
    int depindex = 0;
    char abbrev = '\0';
    const char *str = NULL;
    struct json_object *provides = NULL;
    struct json_object *ref = NULL;

    provides = key_object(file, RPM_FILE_PROVIDES_DESC);

    if (provides == NULL || json_object_get_type(provides) != json_type_array) {
        return 0;
    }

    nprovides = json_object_array_length(provides);

    for (i = 0; i < nprovides; i++) {
        ref = json_object_array_get_idx(provides, i);
        str = key_string(ref, RPM_DEPENDENCY_TYPE_DESC, NULL);

        if (str == NULL) {
            warnx(_("*** missing dependency type in the provides of %s"), path);
            continue;
        }

        abbrev = dependency_type_abbrev(str);

        if (abbrev == '\0') {
            warnx(_("*** unknown dependency type %s in the provides of %s"), str, path);
            continue;
        }

        depindex = dependency_index(build->dependencies, str, ref);

        if (depindex < 0) {
            warnx(_("*** no matching %s dependency for the provides of %s"), str, path);
            continue;
        }

        json_object_array_add(build->out->dependsdict, json_object_new_int64((((uint32_t) abbrev) << DEPENDS_DICT_TYPE_SHIFT) | ((uint32_t) depindex & DEPENDS_DICT_INDEX_MASK)));
        ndepends++;
    }

    return ndepends;
}

/*
 * Add one file entry to the lists.  Every list gets a value so they
 * all stay parallel to the file list.  Entries that are gone from the
 * payload tree are left out.
 */
static void
add_file_entry(struct file_list_build *build, struct json_object *file, const char *path)
{
    bool shared = false;
    size_t ndepends = 0;
    size_t dependsstart = 0;
    int64_t size = 0;
    char *file_path = NULL;
    char *str = NULL;
    struct stat sb;
    struct stat *psb = NULL;
    struct file_list_tags *out = NULL;

    out = build->out;

    /*
     * Files that have been removed from the payload tree since the
     * package was unpacked do not go in to the header.
     */
    if (missing_from_payload(file, path, build->payload_dir)) {
        warnx(_("*** %s is not in the payload, leaving it out of the file list"), path);
        return;
    }

    /*
     * Where this entry lands in the payload tree and what is there
     * now.  The type, the size and the digest all come from it below,
     * so we look it up once.
     */
    if (build->payload_dir != NULL) {
        /* the payload tree holds the paths without a leading slash */
        file_path = joinpath(build->payload_dir, (path[0] == '/') ? path + 1 : path, NULL);

        if (lstat(file_path, &sb) == 0) {
            psb = &sb;
        }
    }

    /*
     * Make entry types match types in the payload subdirectory and
     * fill in the keys the entry does not carry.
     */
    if (psb != NULL) {
        fix_type_mismatch(file, path, file_path, psb, build->digestalgo);
        add_missing_metadata(file, path, file_path, psb, build->digestalgo);
    }

    /* the path splits in to a directory name and a base name */
    add_file_path(out, path);

    size = file_size_value(file, psb);
    json_object_array_add(out->filesizes, json_object_new_int64(size));
    json_object_array_add(out->filemodes, json_object_new_int(file_mode_value(file, psb)));
    json_object_array_add(out->filemtimes, json_object_new_int64(file_mtime_value(file)));
    json_object_array_add(out->fileusernames, json_object_new_string(key_string(file, RPM_FILE_USER_DESC, RPM_FILE_DEFAULT_USER)));
    json_object_array_add(out->filegroupnames, json_object_new_string(key_string(file, RPM_FILE_GROUP_DESC, RPM_FILE_DEFAULT_GROUP)));
    json_object_array_add(out->filerdevs, json_object_new_int((int) key_number(file, RPM_FILE_RDEV_DESC)));
    json_object_array_add(out->filedevices, json_object_new_int64(key_number(file, RPM_FILE_DEVICE_DESC)));

    str = file_digest_value(file, file_path, psb, build->digestalgo);
    json_object_array_add(out->filedigests, json_object_new_string(str));
    free(str);

    /* everything that is not a symlink carries an empty target */
    json_object_array_add(out->filelinktos, json_object_new_string(key_string(file, RPM_FILE_LINKTO_DESC, "")));

    json_object_array_add(out->fileinodes, json_object_new_int(file_inode_value(out, file, (int) (build->nkept + 1), &shared)));

    /*
     * Add up the installed size.  Hard links of one another share the
     * space they take up, so only the first of a group counts.
     */
    if (!shared) {
        build->totalsize += size;
    }

    /* entries with no class point at an empty string */
    json_object_array_add(out->fileclass, json_object_new_int(dict_index(out->classes, &out->nclasses, key_string(file, RPM_FILE_CLASS_DESC, ""))));

    str = file_langs_value(file);
    json_object_array_add(out->filelangs, json_object_new_string(str));
    free(str);

    /*
     * rpm keeps the colors, the flags and the verify flags as
     * bitfields.  Entries with none of them get a zero, except for
     * the verify flags where verifyflag_value() works out what an
     * entry that names nothing means.
     */
    json_object_array_add(out->filecolors, json_object_new_int64(color_value(key_object(file, RPM_FILE_COLORS_DESC))));
    json_object_array_add(out->fileflags, json_object_new_int64(flag_value(key_object(file, RPM_FILE_FLAGS_DESC))));
    json_object_array_add(out->fileverifyflags, json_object_new_int64(verifyflag_value(key_object(file, RPM_FILE_VERIFYFLAGS_DESC))));

    /*
     * rpm keeps the dependencies a file generated as a slice of the
     * depends dictionary, so the file records where its slice starts
     * and how long it is.  rpmbuild writes a zero start for the files
     * that generated nothing.
     */
    dependsstart = json_object_array_length(out->dependsdict);
    ndepends = add_file_provides(build, file, path);

    if (ndepends == 0) {
        dependsstart = 0;
    }

    json_object_array_add(out->filedependsx, json_object_new_int64(dependsstart));
    json_object_array_add(out->filedependsn, json_object_new_int64(ndepends));

    free(file_path);
    build->nkept++;

    return;
}

/*
 * Give back the digest algorithm the header names.  rpm falls back to
 * MD5 when RPMTAG_FILEDIGESTALGO is missing, so we do too.
 */
static uint32_t
file_digest_algo(struct json_object *tags)
{
    const char *str = NULL;

    str = get_tag_value(tags, rpmTagGetName(RPMTAG_FILEDIGESTALGO));

    if (str == NULL) {
        return PGPHASHALGO_MD5;
    }

    return digest_algo(str);
}

/*
 * Walk the "files" array and fill in the lists.  Entries with no path
 * are skipped.
 */
static void
build_file_lists(struct file_list_build *build, struct json_object *files)
{
    size_t i = 0;
    size_t count = 0;
    const char *path = NULL;
    struct json_object *file = NULL;

    count = json_object_array_length(files);

    for (i = 0; i < count; i++) {
        file = json_object_array_get_idx(files, i);
        path = key_string(file, RPM_FILE_PATH_DESC, NULL);

        if (path == NULL) {
            continue;
        }

        add_file_entry(build, file, path);
    }

    return;
}

/*
 * Rebuild the file list tags from the "files" array and add them to
 * the tags array.
 */
void
add_file_list_tags(struct json_object *tags, struct json_object *files, const char *payload_dir, struct json_object *dependencies)
{
    char sizebuf[32];
    bool source_package = false;
    struct file_list_tags out;
    struct file_list_build build;

    if (tags == NULL || files == NULL) {
        return;
    }

    if (json_object_get_type(files) != json_type_array) {
        return;
    }

    if (json_object_array_length(files) == 0) {
        return;
    }

    /* start with empty lists and nothing counted */
    memset(&out, '\0', sizeof(out));
    memset(&build, '\0', sizeof(build));
    new_file_list_arrays(&out, json_object_array_length(files));

    build.out = &out;
    build.dependencies = dependencies;
    build.payload_dir = payload_dir;

    /*
     * We read the digests back out of the payload, so we need the
     * algorithm the header names up front.
     */
    build.digestalgo = file_digest_algo(tags);

    /* walk the files and fill in the lists */
    build_file_lists(&build, files);
    finish_file_list_arrays(&out);

    /*
     * Update the installed size.  The payload tree may have gained,
     * lost or changed files since we unpacked it, so the size in
     * header.json is stale.  Whichever of the two size tags the
     * header has gets the new number.
     */
    snprintf(sizebuf, sizeof(sizebuf), "%" PRId64, build.totalsize);
    set_tag_value(tags, rpmTagGetName(RPMTAG_SIZE), sizebuf);
    set_tag_value(tags, rpmTagGetName(RPMTAG_LONGSIZE), sizebuf);

    /* add the tags in the order rpm writes them */
    add_tag_array(tags, RPMTAG_BASENAMES, RPM_STRING_ARRAY_TYPE, out.basenames);
    add_tag_array(tags, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, out.dirindexes);
    add_tag_array(tags, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE, out.dirnames);
    add_tag_array(tags, RPMTAG_FILESIZES, RPM_INT32_TYPE, out.filesizes);
    add_tag_array(tags, RPMTAG_FILEMODES, RPM_INT16_TYPE, out.filemodes);
    add_tag_array(tags, RPMTAG_FILEMTIMES, RPM_INT32_TYPE, out.filemtimes);
    add_tag_array(tags, RPMTAG_FILEUSERNAME, RPM_STRING_ARRAY_TYPE, out.fileusernames);
    add_tag_array(tags, RPMTAG_FILEGROUPNAME, RPM_STRING_ARRAY_TYPE, out.filegroupnames);
    add_tag_array(tags, RPMTAG_FILERDEVS, RPM_INT16_TYPE, out.filerdevs);
    add_tag_array(tags, RPMTAG_FILEDEVICES, RPM_INT32_TYPE, out.filedevices);
    add_tag_array(tags, RPMTAG_FILEDIGESTS, RPM_STRING_ARRAY_TYPE, out.filedigests);
    add_tag_array(tags, RPMTAG_FILELINKTOS, RPM_STRING_ARRAY_TYPE, out.filelinktos);
    add_tag_array(tags, RPMTAG_FILEINODES, RPM_INT32_TYPE, out.fileinodes);

    /*
     * rpmbuild only classifies the files of binary packages, so a
     * source RPM carries no FILECLASS, CLASSDICT or FILECOLORS tags.
     */
    source_package = (get_tag_value(tags, rpmTagGetName(RPMTAG_SOURCEPACKAGE)) != NULL);

    if (source_package) {
        json_object_put(out.fileclass);
        json_object_put(out.classdict);
    } else {
        add_tag_array(tags, RPMTAG_FILECLASS, RPM_INT32_TYPE, out.fileclass);
        add_tag_array(tags, RPMTAG_CLASSDICT, RPM_STRING_ARRAY_TYPE, out.classdict);
    }

    add_tag_array(tags, RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE, out.filelangs);

    if (source_package) {
        json_object_put(out.filecolors);
    } else {
        add_tag_array(tags, RPMTAG_FILECOLORS, RPM_INT32_TYPE, out.filecolors);
    }

    add_tag_array(tags, RPMTAG_FILEFLAGS, RPM_INT32_TYPE, out.fileflags);
    add_tag_array(tags, RPMTAG_FILEVERIFYFLAGS, RPM_INT32_TYPE, out.fileverifyflags);

    /*
     * rpmbuild only writes the three depends dictionary tags when a
     * file generated a dependency, so we do the same and drop them
     * when the dictionary came out empty.  Source RPMs usually do.
     */
    if (json_object_array_length(out.dependsdict) > 0) {
        add_tag_array(tags, RPMTAG_DEPENDSDICT, RPM_INT32_TYPE, out.dependsdict);
        add_tag_array(tags, RPMTAG_FILEDEPENDSX, RPM_INT32_TYPE, out.filedependsx);
        add_tag_array(tags, RPMTAG_FILEDEPENDSN, RPM_INT32_TYPE, out.filedependsn);
    } else {
        json_object_put(out.dependsdict);
        json_object_put(out.filedependsx);
        json_object_put(out.filedependsn);
    }

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
        case RPMTAG_FILEFLAGS:
        case RPMTAG_FILEVERIFYFLAGS:
        case RPMTAG_DEPENDSDICT:
        case RPMTAG_FILEDEPENDSX:
        case RPMTAG_FILEDEPENDSN:
            return true;
    }

    return false;
}
