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
    uint32_list_t *fileflags;
    uint32_list_t *fileverifyflags;
    uint32_list_t *filedependsx;
    uint32_list_t *filedependsn;

    /*
     * The one list here that is not per-file.  DEPENDSDICT is a single
     * array shared by every file; FILEDEPENDSX and FILEDEPENDSN give
     * the start and length of each file's slice of it.  Track it here
     * since it is part of file metadata.
     */
    uint32_list_t *dependsdict;
};

/*
 * Holds what a walk of the payload tree needs to add entries to the
 * "files" array for the files it finds there.
 */
struct payload_scan {
    struct json_object *files;
    uint32_t digestalgo;
    bool source_package;
};

/*
 * Helper struct for the add_file_list_tags() function.
 * There is one array per file list tag and they all stay parallel to
 * the file list.  The rest of it tracks the things rpm stores once
 * and has each file point at: the directory names, the file classes
 * and the hard link groups.
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
 * array.  The names come from the enum constant names from
 * rpmVerifyAttrs_e with the RPMVERIFY_ prefix trimmed and lowercased.
 * This is also close to how it would appear in a spec file, just
 * without the '%' prefix.
 *
 * Only bits 0 through 8 appear here because those are the only ones a
 * spec file can specify; they are what %verify() accepts.  The higher
 * bits rpmVerifyAttrs_e defines are used internally by rpm and are
 * never read back out of this tag.  They are in the stored value
 * though: rpmbuild defaults every file to RPMVERIFY_ALL, which is ~0,
 * and writes %verify(not ...) as the complement of the listed bits,
 * so a file that verifies everything carries 0xffffffff rather than
 * an explicit list of the attributes it wants checked.  Fun.
 *
 * RPMVERIFY_MD5 is left out here because it is an obsolete spelling
 * of RPMVERIFY_FILEDIGEST and shares the same bit; it is still
 * accepted when reading.
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
 * Returns the name for the file type bits of a mode.  rpmfiWhatis()
 * decides which type it is so tarpm reads a mode the way rpm does.
 * Anything that is not one of the types rpm knows is a regular file.
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
 * Convert a FILEFLAGS value in to an array of flag name strings.  The
 * names come from the rpmfileAttrs_e constants with the RPMFILE_
 * prefix trimmed and lowercased.  Bits with no name are carried as a
 * single decimal number to keep the value intact across a round trip.
 * Returns NULL for a flags value of zero, which callers use to omit
 * the key.
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
 * Convert a FILEVERIFYFLAGS value in to an array of verify flag name
 * strings.  The names come from the rpmVerifyAttrs_e constants with
 * the RPMVERIFY_ prefix trimmed and lowercased.  Only the named bits
 * are reported; anything above them is dropped rather than carried
 * along as a number, so what lands in header.json is the list of
 * attributes rpm will actually check.  Returns NULL when none of the
 * named bits are set, which callers use to omit the key.
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
 * Convert an array of verify flag name strings back in to a
 * FILEVERIFYFLAGS value.  Only the names verifyflag_names() emits are
 * understood, plus "md5" as the obsolete spelling of "filedigest".
 * rpmbuild starts every file at RPMVERIFY_ALL and clears the bits the
 * spec file asked it to skip, so the value is built the same way here:
 * every bit outside the named ones stays set and the named bits the
 * array does not list are cleared.  A missing or empty array means
 * none of the named attributes are verified.
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
 * Convert one file's slice of the depends dictionary in to an array
 * of the dependencies that file generated.  Each dictionary value
 * names a dependency type in its high byte and an index in to that
 * type's array in the "dependencies" object in the rest, so every
 * value resolves to exactly one dependency.  The resolved dependency
 * is copied verbatim and given a "type" key naming the array it came
 * from.
 *
 * The order of the slice is kept as-is because rpm records these in
 * whatever order the dependency generators produced them, which means
 * the types can interleave and the same dependency can appear more
 * than once.  Reproducing the tag on create needs that order back.
 *
 * Returns NULL when the slice is empty or nothing in it resolves,
 * which callers use to omit the key.
 */
static struct json_object *
file_dependencies(struct json_object *dependencies, const uint32_list_t *dependsdict, uint32_t start, uint32_t count)
{
    uint32_t i = 0;
    uint32_t value = 0;
    uint32_t index = 0;
    char abbrev = '\0';
    const char *type = NULL;
    struct json_object *deps = NULL;
    struct json_object *entry = NULL;
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

        abbrev = (char) ((value >> DEPENDS_DICT_TYPE_SHIFT) & 0xFF);
        index = value & DEPENDS_DICT_INDEX_MASK;
        type = dependency_type_key(abbrev);

        if (type == NULL) {
            warnx(_("*** unknown dependency type '%c' in the depends dictionary"), abbrev);
            continue;
        }

        if (!json_object_object_get_ex(dependencies, type, &deps)) {
            warnx(_("*** no %s dependencies for the depends dictionary"), type);
            continue;
        }

        entry = json_object_array_get_idx(deps, index);

        if (entry == NULL) {
            warnx(_("*** %s dependency %u is out of range"), type, index);
            continue;
        }

        /* copy the dependency, naming the array it came from first */
        ref = json_object_new_object();
        json_object_object_add(ref, RPM_DEPENDENCY_TYPE_DESC, json_object_new_string(type));

        json_object_object_foreach(entry, depkey, depval) {
            json_object_object_add(ref, depkey, json_object_get(depval));
        }

        if (refs == NULL) {
            refs = json_object_new_array();
        }

        json_object_array_add(refs, ref);
    }

    return refs;
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
    struct json_object *flags_array = NULL;
    struct json_object *verifyflags_array = NULL;
    struct json_object *provides_array = NULL;
    uint32_entry_t *dirindex = NULL;
    uint32_entry_t *filesize = NULL;
    uint32_entry_t *filemode = NULL;
    uint32_entry_t *filemtime = NULL;
    uint32_entry_t *filerdev = NULL;
    uint32_entry_t *filedevice = NULL;
    uint32_entry_t *fileinode = NULL;
    uint32_entry_t *fileclass = NULL;
    uint32_entry_t *filecolor = NULL;
    uint32_entry_t *fileflag = NULL;
    uint32_entry_t *fileverifyflag = NULL;
    uint32_entry_t *filedependsx = NULL;
    uint32_entry_t *filedependsn = NULL;
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
        } else if (tag == RPMTAG_FILEFLAGS && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.fileflags = uint32_list_add(fmd.fileflags, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEVERIFYFLAGS && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.fileverifyflags = uint32_list_add(fmd.fileverifyflags, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEDEPENDSX && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filedependsx = uint32_list_add(fmd.filedependsx, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_FILEDEPENDSN && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.filedependsn = uint32_list_add(fmd.filedependsn, ntohl(val32));
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_DEPENDSDICT && datatype == RPM_INT32_TYPE) {
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&val32, p, sizeof(uint32_t));
                fmd.dependsdict = uint32_list_add(fmd.dependsdict, ntohl(val32));
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
    fileflag = first_uint32(fmd.fileflags);
    fileverifyflag = first_uint32(fmd.fileverifyflags);
    filedependsx = first_uint32(fmd.filedependsx);
    filedependsn = first_uint32(fmd.filedependsn);

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
            json_object_object_add(file, RPM_FILE_PATH_DESC, json_object_new_string(path));

            /*
             * Add size for regular files and symlinks.  RPM stores the
             * length of a symlink's target string in FILESIZES, and the
             * payload reader consumes that many bytes for the target, so
             * the value must be preserved for symlinks too.
             */
            if (filesize != NULL && filemode != NULL) {
                if (S_ISREG(filemode->value) || S_ISLNK(filemode->value)) {
                    json_object_object_add(file, RPM_FILE_SIZE_DESC, json_object_new_int64(filesize->value));
                }
            }

            /*
             * Add mode if available (as octal string of permission bits
             * only).  The file type bits of the mode go alongside it
             * under their own name.
             */
            if (filemode != NULL) {
                snprintf(mode_str, sizeof(mode_str), RPM_FILE_MODE_FORMAT, filemode->value & ALLPERMS);
                json_object_object_add(file, RPM_FILE_MODE_DESC, json_object_new_string(mode_str));
                json_object_object_add(file, RPM_FILE_TYPE_DESC, json_object_new_string(type_name(filemode->value)));
            }

            /* Add mtime if available (as ISO 8601 timestamp) */
            if (filemtime != NULL) {
                mtime = (time_t) filemtime->value;
                tm_info = gmtime(&mtime);

                if (tm_info != NULL) {
                    strftime(mtime_str, sizeof(mtime_str), RPM_FILE_MTIME_FORMAT, tm_info);
                    json_object_object_add(file, RPM_FILE_MTIME_DESC, json_object_new_string(mtime_str));
                }
            }

            /* Add user if available */
            if (username != NULL) {
                json_object_object_add(file, RPM_FILE_USER_DESC, json_object_new_string(username->str));
            }

            /* Add group if available */
            if (groupname != NULL) {
                json_object_object_add(file, RPM_FILE_GROUP_DESC, json_object_new_string(groupname->str));
            }

            /* Add rdev if available (only for device nodes with non-zero values) */
            if (filerdev != NULL && filemode != NULL) {
                if ((S_ISCHR(filemode->value) || S_ISBLK(filemode->value)) && filerdev->value != 0) {
                    json_object_object_add(file, RPM_FILE_RDEV_DESC, json_object_new_int(filerdev->value));
                }
            }

            /* Add device if available */
            if (filedevice != NULL) {
                json_object_object_add(file, RPM_FILE_DEVICE_DESC, json_object_new_int64(filedevice->value));
            }

            /*
             * Add digest for regular files that have one.  RPM stores an
             * empty string for entries without a digest (directories,
             * symlinks, device nodes, etc.), so only emit the key when the
             * digest is non-empty.
             */
            if (digest != NULL && digest->str[0] != '\0') {
                json_object_object_add(file, RPM_FILE_DIGEST_DESC, json_object_new_string(digest->str));
            }

            /*
             * Add linkto for symbolic links.  RPM stores an empty string
             * for entries that are not symlinks, so only emit the key when
             * the target is non-empty.
             */
            if (linkto != NULL && linkto->str[0] != '\0') {
                json_object_object_add(file, RPM_FILE_LINKTO_DESC, json_object_new_string(linkto->str));
            }

            /*
             * Add inode for every entry.  RPM assigns these numbers itself
             * (starting at 1 and incrementing) to track hard links; files
             * that share an inode number are hard links of one another.
             */
            if (fileinode != NULL) {
                json_object_object_add(file, RPM_FILE_INODE_DESC, json_object_new_int(fileinode->value));
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
                    json_object_object_add(file, RPM_FILE_CLASS_DESC, json_object_new_string(classname));
                }
            }

            /*
             * Add langs for entries that carry one.  RPM stores the
             * languages of a file as a single string with each language
             * separated by a "|", so split that in to an array here.
             * Entries with no language carry an empty string.
             */
            if (filelang != NULL && filelang->str[0] != '\0') {
                langs = strsplit(filelang->str, RPM_FILE_LANG_SEPARATOR);

                if (langs != NULL) {
                    langs_array = json_object_new_array();

                    TAILQ_FOREACH(lang, langs, items) {
                        json_object_array_add(langs_array, json_object_new_string(lang->str));
                    }

                    json_object_object_add(file, RPM_FILE_LANGS_DESC, langs_array);
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
                    json_object_object_add(file, RPM_FILE_COLORS_DESC, colors_array);
                    colors_array = NULL;
                }
            }

            /*
             * Add flags for entries that carry one.  RPM records the
             * %config, %doc, %ghost and similar markings of a file as a
             * bitfield, so turn that in to an array of names here.
             * Entries with no flags carry a zero, which flag_names()
             * reports as NULL so the key is left off.
             */
            if (fileflag != NULL) {
                flags_array = flag_names(fileflag->value);

                if (flags_array != NULL) {
                    json_object_object_add(file, RPM_FILE_FLAGS_DESC, flags_array);
                    flags_array = NULL;
                }
            }

            /*
             * Add verifyflags for entries that carry one.  RPM records
             * the %verify() settings of a file as a bitfield, so turn
             * that in to an array of names here.  Entries that verify
             * nothing carry a zero, which verifyflag_names() reports as
             * NULL so the key is left off.
             */
            if (fileverifyflag != NULL) {
                verifyflags_array = verifyflag_names(fileverifyflag->value);

                if (verifyflags_array != NULL) {
                    json_object_object_add(file, RPM_FILE_VERIFYFLAGS_DESC, verifyflags_array);
                    verifyflags_array = NULL;
                }
            }

            /*
             * Add provides for entries that generated dependencies.
             * FILEDEPENDSX and FILEDEPENDSN hold the start and the
             * length of this file's slice of the depends dictionary,
             * so resolve that slice to the dependencies it names here.
             * Entries that generated nothing carry a length of zero,
             * so the key is left off for them.
             */
            if (filedependsx != NULL && filedependsn != NULL && filedependsn->value > 0) {
                provides_array = file_dependencies(dependencies, fmd.dependsdict, filedependsx->value, filedependsn->value);

                if (provides_array != NULL) {
                    json_object_object_add(file, RPM_FILE_PROVIDES_DESC, provides_array);
                    provides_array = NULL;
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
        fileflag = next_uint32(fileflag);
        fileverifyflag = next_uint32(fileverifyflag);
        filedependsx = next_uint32(filedependsx);
        filedependsn = next_uint32(filedependsn);
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
 * Returns true if the file an entry in the "files" array describes is
 * gone from the payload tree.  %ghost files are never carried in the
 * payload, so those are always reported as present; anything else that
 * is not there was removed from the unpacked package and should not go
 * in to the header.  Entries are also reported as present when there is
 * no payload tree to look in.
 */
static bool
missing_from_payload(struct json_object *file, const char *path, const char *input_dir, const char *payload_subdir)
{
    char *file_path = NULL;
    bool missing = false;
    struct json_object *flags_obj = NULL;
    struct stat sb;

    if (file == NULL || path == NULL || input_dir == NULL || payload_subdir == NULL) {
        return false;
    }

    if (json_object_object_get_ex(file, RPM_FILE_FLAGS_DESC, &flags_obj) && (flag_value(flags_obj) & RPMFILE_GHOST)) {
        return false;
    }

    file_path = joinpath(input_dir, payload_subdir, (path[0] == '/') ? path + 1 : path, NULL);
    missing = (lstat(file_path, &sb) != 0);
    free(file_path);

    return missing;
}

/*
 * Returns true if the "files" array needs an entry for this file in
 * the payload tree.  Paths the list already names do not need one.
 * Neither do the directories that lead to a file the list names; the
 * payload tree has to have them to hold the files under them, but the
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
    struct json_object *path_obj = NULL;

    count = json_object_array_length(files);
    len = strlen(path);

    for (i = 0; i < count; i++) {
        file = json_object_array_get_idx(files, i);

        if (!json_object_object_get_ex(file, RPM_FILE_PATH_DESC, &path_obj)) {
            continue;
        }

        s = json_object_get_string(path_obj);

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
     * The payload tree is unpacked as whoever ran tarpm, so who owns
     * the file now says nothing about who should own it once the
     * package is installed.  Added files go to the default owner.
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
     * Files that are hard links of one another share an inode number,
     * which is the only thing the "inode" key is read for.  Entries
     * with a link count of one do not need it.
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
add_payload_files(struct json_object *tags, struct json_object *files, const char *input_dir, const char *payload_subdir)
{
    char *payload_dir = NULL;
    const char *algo = NULL;
    struct payload_scan scan = { 0 };
    struct stat sb;

    if (tags == NULL || files == NULL || input_dir == NULL || payload_subdir == NULL) {
        return;
    }

    if (json_object_get_type(files) != json_type_array) {
        return;
    }

    payload_dir = joinpath(input_dir, payload_subdir, NULL);

    if (lstat(payload_dir, &sb) == -1 || !S_ISDIR(sb.st_mode)) {
        free(payload_dir);
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

    free(payload_dir);
    return;
}

/*
 * Update any inconsistencies with file metadata in the JSON
 * structures with what is on the actual files in the payload
 * subdirectory.
 *
 * The metadata checked and updated if necessary is size, type, linkto
 * (for symbolic links), and rdev.  For payload members that
 * completely change types, the metadata is adjusted accordingly to
 * match the new type.
 */
static void
fix_type_mismatch(struct json_object *file, const char *path, const char *file_path, const struct stat *sb, const uint32_t digestalgo)
{
    ssize_t len = 0;
    char *target = NULL;
    char *digest = NULL;
    struct json_object *type_obj = NULL;
    const char *entry_type = NULL;
    const char *payload_type = NULL;

    if (!json_object_object_get_ex(file, RPM_FILE_TYPE_DESC, &type_obj)) {
        return;
    }

    entry_type = json_object_get_string(type_obj);
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
 * Reconstruct the file list tag entries from the files array.
 * Adds the file list tag entries (DIRNAMES, BASENAMES, DIRINDEXES, FILESIZES, FILEMODES, FILEMTIMES)
 * to the provided tags array.
 */
void
add_file_list_tags(struct json_object *tags, struct json_object *files, const char *input_dir, const char *payload_subdir, struct json_object *dependencies)
{
    size_t i = 0;
    size_t j = 0;
    size_t count = 0;
    size_t nkept = 0;
    size_t nlangs = 0;
    size_t nprovides = 0;
    size_t ndepends = 0;
    size_t dependsstart = 0;
    struct file_list_tags out = { 0 };
    struct json_object *file = NULL;
    struct json_object *value = NULL;
    struct json_object *provides = NULL;
    struct json_object *ref = NULL;
    struct json_object *tag = NULL;
    str_list_t *langs = NULL;
    char *joined = NULL;
    const char *path = NULL;
    const char *basename = NULL;
    const char *dirname = NULL;
    const char *str = NULL;
    const char *separator = NULL;
    char *dirname_copy = NULL;
    char *file_path = NULL;
    char *computed_digest = NULL;
    char sizebuf[32];
    uint32_t digestalgo = 0;
    uint32_t bits = 0;
    char abbrev = '\0';
    struct stat sb;
    struct tm tm_info;
    int dictindex = 0;
    int depindex = 0;
    int mode = 0;
    int perms = 0;
    mode_t typebits = 0;
    int rdev = 0;
    int64_t device = 0;
    int inode = 0;
    int64_t size = 0;
    int64_t totalsize = 0;
    time_t mtime = 0;
    bool found = false;
    bool have_stat = false;
    bool source_package = false;

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

    /*
     * rpmbuild only classifies the files of binary packages, so a
     * source RPM carries no FILECLASS, CLASSDICT or FILECOLORS tags.
     */
    source_package = (get_tag_value(tags, rpmTagGetName(RPMTAG_SOURCEPACKAGE)) != NULL);

    /*
     * The file digests are recomputed from the actual contents of the
     * payload, so the algorithm the header names for them is needed
     * up front.  rpm defaults to MD5 for packages missing the
     * RPMTAG_FILEDIGESTALGO tag.
     */
    str = get_tag_value(tags, rpmTagGetName(RPMTAG_FILEDIGESTALGO));

    if (str == NULL) {
        digestalgo = PGPHASHALGO_MD5;
    } else {
        digestalgo = digest_algo(str);
    }

    /* Allocate arrays for unique directory, class and inode tracking */
    out.dirs = xcalloc(count, sizeof(char *));
    out.classes = xcalloc(count, sizeof(char *));
    out.inodes = xcalloc(count, sizeof(int));
    out.mapped = xcalloc(count, sizeof(int));

    /* Create the arrays */
    out.dirnames = json_object_new_array();
    out.basenames = json_object_new_array();
    out.dirindexes = json_object_new_array();
    out.filesizes = json_object_new_array();
    out.filemodes = json_object_new_array();
    out.filemtimes = json_object_new_array();
    out.fileusernames = json_object_new_array();
    out.filegroupnames = json_object_new_array();
    out.filerdevs = json_object_new_array();
    out.filedevices = json_object_new_array();
    out.filedigests = json_object_new_array();
    out.filelinktos = json_object_new_array();
    out.fileinodes = json_object_new_array();
    out.fileclass = json_object_new_array();
    out.classdict = json_object_new_array();
    out.filelangs = json_object_new_array();
    out.filecolors = json_object_new_array();
    out.fileflags = json_object_new_array();
    out.fileverifyflags = json_object_new_array();
    out.filedependsx = json_object_new_array();
    out.filedependsn = json_object_new_array();
    out.dependsdict = json_object_new_array();

    /* Process each file entry */
    for (i = 0; i < count; i++) {
        file = json_object_array_get_idx(files, i);

        if (!json_object_object_get_ex(file, RPM_FILE_PATH_DESC, &value)) {
            continue;
        }

        path = json_object_get_string(value);

        /*
         * Files that have been removed from the payload tree since the
         * package was unpacked do not go in to the header.
         */
        if (missing_from_payload(file, path, input_dir, payload_subdir)) {
            warnx(_("*** %s is not in the payload, leaving it out of the file list"), path);
            continue;
        }

        /*
         * Where this entry lands in the payload tree and what is
         * sitting there.  The type, the size and the digest all come
         * from it below, so look it up the once.
         */
        file_path = NULL;
        have_stat = false;

        if (input_dir != NULL && payload_subdir != NULL) {
            /* Strip leading slash from path for payload lookup */
            file_path = joinpath(input_dir, payload_subdir, (path[0] == '/') ? path + 1 : path, NULL);
            have_stat = (lstat(file_path, &sb) == 0);
        }

        /*
         * Make entry types match types in the payload subdirectory
         * and fill in the keys the entry does not carry.
         */
        if (have_stat) {
            fix_type_mismatch(file, path, file_path, &sb, digestalgo);
            add_missing_metadata(file, path, file_path, &sb, digestalgo);
        }

        /* Find the last separator to split dirname and basename */
        separator = strrchr(path, '/');

        if (separator == NULL) {
            /*
             * No directory separator.  Source RPMs are stored this way;
             * they carry a single empty dirname and the bare filenames
             * as the basenames.
             */
            dirname = "";
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

        /* Find or add dirname to the directory names */
        dictindex = -1;
        found = false;

        for (j = 0; j < out.ndirs; j++) {
            if (strcmp(out.dirs[j], dirname) == 0) {
                dictindex = j;
                found = true;
                break;
            }
        }

        if (!found) {
            /* New directory */
            out.dirs[out.ndirs] = strdup(dirname);
            dictindex = out.ndirs;
            out.ndirs++;
        }

        /* Add basename and dirindex */
        json_object_array_add(out.basenames, json_object_new_string(basename));
        json_object_array_add(out.dirindexes, json_object_new_int(dictindex));

        /*
         * Add size (regular files have size, non-files get 0).  The
         * size comes from the regular file in the payload directory
         * so an edited payload gets the correct size in the header.
         */
        size = 0;

        if (json_object_object_get_ex(file, RPM_FILE_SIZE_DESC, &value)) {
            size = json_object_get_int64(value);

            if (have_stat && S_ISREG(sb.st_mode)) {
                size = (int64_t) sb.st_size;
            }
        }

        json_object_array_add(out.filesizes, json_object_new_int64(size));

        /* Reconstruct full mode from permission bits and actual file type */
        mode = 0;

        if (json_object_object_get_ex(file, RPM_FILE_MODE_DESC, &value)) {
            /* Parse octal permission string */
            perms = (int) strtol(json_object_get_string(value), NULL, 8);

            /* the file in the payload tree says what type it is */
            typebits = 0;

            if (have_stat) {
                typebits = sb.st_mode & S_IFMT;
            }

            /*
             * Nothing in the payload to look at, so go by the type the
             * file list names.  %ghost entries land here.
             */
            if (typebits == 0 && json_object_object_get_ex(file, RPM_FILE_TYPE_DESC, &value)) {
                typebits = type_bits(json_object_get_string(value));
            }

            /*
             * A file list from an older tarpm names no type, so fall
             * back to the guess it used to make: an entry with a size
             * is a regular file and anything else is a directory.
             */
            if (typebits == 0) {
                if (json_object_object_get_ex(file, RPM_FILE_SIZE_DESC, NULL)) {
                    typebits = S_IFREG;
                } else {
                    typebits = S_IFDIR;
                }
            }

            mode = typebits | (perms & ALLPERMS);
        }

        json_object_array_add(out.filemodes, json_object_new_int(mode));

        /* Parse mtime from ISO 8601 timestamp string */
        mtime = 0;

        if (json_object_object_get_ex(file, RPM_FILE_MTIME_DESC, &value)) {
            memset(&tm_info, 0, sizeof(struct tm));

            if (strptime(json_object_get_string(value), RPM_FILE_MTIME_FORMAT, &tm_info) != NULL) {
                mtime = timegm(&tm_info);
            }
        }

        json_object_array_add(out.filemtimes, json_object_new_int64(mtime));

        /* Extract user if available */
        str = NULL;

        if (json_object_object_get_ex(file, RPM_FILE_USER_DESC, &value)) {
            str = json_object_get_string(value);
        }

        if (str != NULL) {
            json_object_array_add(out.fileusernames, json_object_new_string(str));
        } else {
            json_object_array_add(out.fileusernames, json_object_new_string(RPM_FILE_DEFAULT_USER));
        }

        /* Extract group if available */
        str = NULL;

        if (json_object_object_get_ex(file, RPM_FILE_GROUP_DESC, &value)) {
            str = json_object_get_string(value);
        }

        if (str != NULL) {
            json_object_array_add(out.filegroupnames, json_object_new_string(str));
        } else {
            json_object_array_add(out.filegroupnames, json_object_new_string(RPM_FILE_DEFAULT_GROUP));
        }

        /* Extract rdev if available */
        rdev = 0;

        if (json_object_object_get_ex(file, RPM_FILE_RDEV_DESC, &value)) {
            rdev = json_object_get_int(value);
        }

        json_object_array_add(out.filerdevs, json_object_new_int(rdev));

        /* Extract device if available */
        device = 0;

        if (json_object_object_get_ex(file, RPM_FILE_DEVICE_DESC, &value)) {
            device = json_object_get_int64(value);
        }

        json_object_array_add(out.filedevices, json_object_new_int64(device));

        /*
         * Extract digest if available.  Entries without a "digest" key
         * (directories, symlinks, device nodes, etc.) carry an empty
         * string so the array stays parallel to the file list.
         */
        str = NULL;
        computed_digest = NULL;

        if (json_object_object_get_ex(file, RPM_FILE_DIGEST_DESC, &value)) {
            str = json_object_get_string(value);
        }

        /*
         * Entries carrying a digest get it recomputed from the file in
         * the payload so an edited payload lands a correct digest in
         * the header.  The digest header.json recorded is kept if
         * there is no regular file in the payload to read.
         */
        if (str != NULL && str[0] != '\0' && digestalgo != 0 && have_stat && S_ISREG(sb.st_mode)) {
            computed_digest = mkfiledigest(file_path, digestalgo);

            if (computed_digest != NULL) {
                str = computed_digest;
            }
        }

        if (str != NULL) {
            json_object_array_add(out.filedigests, json_object_new_string(str));
        } else {
            json_object_array_add(out.filedigests, json_object_new_string(""));
        }

        free(computed_digest);

        /*
         * Extract linkto if available.  Entries without a "linkto" key
         * (everything that is not a symlink) carry an empty string so
         * the array stays parallel to the file list.
         */
        str = NULL;

        if (json_object_object_get_ex(file, RPM_FILE_LINKTO_DESC, &value)) {
            str = json_object_get_string(value);
        }

        if (str != NULL) {
            json_object_array_add(out.filelinktos, json_object_new_string(str));
        } else {
            json_object_array_add(out.filelinktos, json_object_new_string(""));
        }

        /*
         * Assign the inode.  RPM numbers these itself: an entry gets
         * its one based position in the file list, and entries that are
         * hard links of one another all carry the number the first of
         * them got.  Numbering here rather than carrying the value in
         * header.json over keeps the numbers right when entries have
         * been left out, so the "inode" key is only read to tell which
         * entries were hard links of one another.
         */
        inode = (int) (nkept + 1);

        if (json_object_object_get_ex(file, RPM_FILE_INODE_DESC, &value)) {
            found = false;

            for (j = 0; j < out.ninodes; j++) {
                if (out.inodes[j] == json_object_get_int(value)) {
                    inode = out.mapped[j];
                    found = true;
                    break;
                }
            }

            if (!found) {
                /* the first entry of a hard link group names the group */
                out.inodes[out.ninodes] = json_object_get_int(value);
                out.mapped[out.ninodes] = inode;
                out.ninodes++;
            }
        }

        json_object_array_add(out.fileinodes, json_object_new_int(inode));

        /*
         * Add up the installed size.  Hard links of one another share
         * the space they take up, so only the first of a group counts.
         */
        if (!json_object_object_get_ex(file, RPM_FILE_INODE_DESC, NULL) || !found) {
            totalsize += size;
        }

        /*
         * Extract class and rebuild the CLASSDICT/FILECLASS pair the way
         * librpm does: CLASSDICT holds the unique class strings in order
         * of first appearance and FILECLASS holds each file's index into
         * it.  Entries without a "class" key use an empty string.
         */
        str = "";

        if (json_object_object_get_ex(file, RPM_FILE_CLASS_DESC, &value)) {
            str = json_object_get_string(value);
        }

        dictindex = -1;
        found = false;

        for (j = 0; j < out.nclasses; j++) {
            if (strcmp(out.classes[j], str) == 0) {
                dictindex = j;
                found = true;
                break;
            }
        }

        if (!found) {
            out.classes[out.nclasses] = strdup(str);
            dictindex = out.nclasses;
            out.nclasses++;
        }

        json_object_array_add(out.fileclass, json_object_new_int(dictindex));

        /*
         * Extract langs if available.  The "langs" value is an array of
         * language strings which RPM stores as a single string with each
         * language separated by a "|".  Entries without a "langs" key
         * carry an empty string so the array stays parallel to the file
         * list.
         */
        joined = NULL;

        if (json_object_object_get_ex(file, RPM_FILE_LANGS_DESC, &value) && json_object_get_type(value) == json_type_array) {
            nlangs = json_object_array_length(value);

            for (j = 0; j < nlangs; j++) {
                langs = list_add(langs, json_object_get_string(json_object_array_get_idx(value, j)));
            }

            joined = list_to_string(langs, RPM_FILE_LANG_SEPARATOR);
            list_free(langs, free);
            langs = NULL;
        }

        if (joined != NULL) {
            json_object_array_add(out.filelangs, json_object_new_string(joined));
            free(joined);
            joined = NULL;
        } else {
            json_object_array_add(out.filelangs, json_object_new_string(""));
        }

        /*
         * Extract colors if available.  The "colors" value is an array
         * of color names which RPM stores as a bitfield.  Entries
         * without a "colors" key carry a zero so the array stays
         * parallel to the file list.
         */
        bits = 0;

        if (json_object_object_get_ex(file, RPM_FILE_COLORS_DESC, &value)) {
            bits = color_value(value);
        }

        json_object_array_add(out.filecolors, json_object_new_int64(bits));

        /*
         * Extract flags if available.  The "flags" value is an array of
         * flag names which RPM stores as a bitfield.  Entries without a
         * "flags" key carry a zero so the array stays parallel to the
         * file list.
         */
        bits = 0;

        if (json_object_object_get_ex(file, RPM_FILE_FLAGS_DESC, &value)) {
            bits = flag_value(value);
        }

        json_object_array_add(out.fileflags, json_object_new_int64(bits));

        /*
         * Extract verifyflags if available.  The "verifyflags" value is
         * an array of verify flag names which RPM stores as a bitfield.
         * Entries without a "verifyflags" key verify none of the named
         * attributes, which verifyflag_value() handles, so the array
         * stays parallel to the file list.
         */
        value = NULL;
        json_object_object_get_ex(file, RPM_FILE_VERIFYFLAGS_DESC, &value);
        bits = verifyflag_value(value);

        json_object_array_add(out.fileverifyflags, json_object_new_int64(bits));

        /*
         * Extract provides if available.  The "provides" value is the
         * ordered list of dependencies this file generated, which RPM
         * keeps as a slice of the depends dictionary.  Each entry
         * becomes one dictionary value naming its type and its index
         * in the matching "dependencies" array, and the file records
         * where its slice starts and how long it is.  Entries without
         * a "provides" key get a length of zero, and rpmbuild writes a
         * zero start for those too.
         */
        dependsstart = json_object_array_length(out.dependsdict);
        ndepends = 0;

        if (json_object_object_get_ex(file, RPM_FILE_PROVIDES_DESC, &provides) && json_object_get_type(provides) == json_type_array) {
            nprovides = json_object_array_length(provides);

            for (j = 0; j < nprovides; j++) {
                ref = json_object_array_get_idx(provides, j);

                if (!json_object_object_get_ex(ref, RPM_DEPENDENCY_TYPE_DESC, &value)) {
                    warnx(_("*** missing dependency type in the provides of %s"), path);
                    continue;
                }

                str = json_object_get_string(value);
                abbrev = dependency_type_abbrev(str);

                if (abbrev == '\0') {
                    warnx(_("*** unknown dependency type %s in the provides of %s"), str, path);
                    continue;
                }

                depindex = dependency_index(dependencies, str, ref);

                if (depindex < 0) {
                    warnx(_("*** no matching %s dependency for the provides of %s"), str, path);
                    continue;
                }

                json_object_array_add(out.dependsdict, json_object_new_int64((((uint32_t) abbrev) << DEPENDS_DICT_TYPE_SHIFT) | ((uint32_t) depindex & DEPENDS_DICT_INDEX_MASK)));
                ndepends++;
            }
        }

        if (ndepends == 0) {
            dependsstart = 0;
        }

        json_object_array_add(out.filedependsx, json_object_new_int64(dependsstart));
        json_object_array_add(out.filedependsn, json_object_new_int64(ndepends));

        if (dirname_copy) {
            free(dirname_copy);
            dirname_copy = NULL;
        }

        free(file_path);
        file_path = NULL;

        nkept++;
    }

    free(out.inodes);
    free(out.mapped);

    /* Build the dirnames array from the directory names */
    for (i = 0; i < out.ndirs; i++) {
        json_object_array_add(out.dirnames, json_object_new_string(out.dirs[i]));
        free(out.dirs[i]);
    }

    free(out.dirs);

    /* Build the classdict array from the class strings */
    for (i = 0; i < out.nclasses; i++) {
        json_object_array_add(out.classdict, json_object_new_string(out.classes[i]));
        free(out.classes[i]);
    }

    free(out.classes);

    /*
     * Update the installed size of the package.  Files may have been
     * added to, edited in or taken out of the payload tree since it
     * was unpacked, so the size header.json carries is out of date.
     * Whichever of the two size tags the header has gets the new
     * number and a header without either one is left alone.
     */
    snprintf(sizebuf, sizeof(sizebuf), "%" PRId64, totalsize);
    set_tag_value(tags, rpmTagGetName(RPMTAG_SIZE), sizebuf);
    set_tag_value(tags, rpmTagGetName(RPMTAG_LONGSIZE), sizebuf);

    /* Add RPMTAG_BASENAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_BASENAMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.basenames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_DIRINDEXES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DIRINDEXES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.dirindexes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_DIRNAMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DIRNAMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.dirnames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILESIZES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILESIZES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filesizes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEMODES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEMODES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT16_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filemodes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEMTIMES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEMTIMES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filemtimes);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEUSERNAME tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEUSERNAME)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.fileusernames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEGROUPNAME tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEGROUPNAME)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filegroupnames);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILERDEVS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILERDEVS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT16_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filerdevs);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEDEVICES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDEVICES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filedevices);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEDIGESTS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDIGESTS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filedigests);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILELINKTOS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILELINKTOS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filelinktos);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEINODES tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEINODES)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.fileinodes);
    json_object_array_add(tags, tag);

    if (source_package) {
        json_object_put(out.fileclass);
        json_object_put(out.classdict);
    } else {
        /* Add RPMTAG_FILECLASS tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILECLASS)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.fileclass);
        json_object_array_add(tags, tag);

        /* Add RPMTAG_CLASSDICT tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_CLASSDICT)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.classdict);
        json_object_array_add(tags, tag);
    }

    /* Add RPMTAG_FILELANGS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILELANGS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filelangs);
    json_object_array_add(tags, tag);

    if (source_package) {
        json_object_put(out.filecolors);
    } else {
        /* Add RPMTAG_FILECOLORS tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILECOLORS)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filecolors);
        json_object_array_add(tags, tag);
    }

    /* Add RPMTAG_FILEFLAGS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEFLAGS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.fileflags);
    json_object_array_add(tags, tag);

    /* Add RPMTAG_FILEVERIFYFLAGS tag */
    tag = json_object_new_object();
    json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEVERIFYFLAGS)));
    json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.fileverifyflags);
    json_object_array_add(tags, tag);

    /*
     * Add the RPMTAG_DEPENDSDICT, RPMTAG_FILEDEPENDSX, and
     * RPMTAG_FILEDEPENDSN tags.  rpmbuild only writes these three when
     * at least one file generated a dependency, so do the same and
     * drop them when the dictionary came out empty.  Source RPMs are
     * the common case for that.
     */
    if (json_object_array_length(out.dependsdict) > 0) {
        /* Add RPMTAG_DEPENDSDICT tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DEPENDSDICT)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.dependsdict);
        json_object_array_add(tags, tag);

        /* Add RPMTAG_FILEDEPENDSX tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDEPENDSX)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filedependsx);
        json_object_array_add(tags, tag);

        /* Add RPMTAG_FILEDEPENDSN tag */
        tag = json_object_new_object();
        json_object_object_add(tag, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_FILEDEPENDSN)));
        json_object_object_add(tag, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT32_TYPE)));
        json_object_object_add(tag, RPM_ENTRY_VALUE_DESC, out.filedependsn);
        json_object_array_add(tags, tag);
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
