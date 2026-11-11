/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>
#include <rpm/rpmtd.h>
#include <json.h>
#include <time.h>

#include "tarpm.h"

/*
 * Tracks the size of tags whose data lives in an external file.  We
 * need this when building the header.
 */
struct tagfile {
    uint8_t *data;    /* file contents, always NUL terminated */
    size_t len;       /* byte length, not counting the trailing NUL */
    uint32_t count;   /* how many strings data holds, one after another */
    bool present;     /* true once the file has been read */
};

/*
 * Where we are in the data area while we fill it in.  The offset is
 * what the index entry records and datapos is the matching spot in
 * the buffer, so the two always move together.
 */
struct data_writer {
    uint8_t *datapos;
    int32_t offset;
};

static rpmTagType
get_entry_type(struct json_object *entry)
{
    rpmTagVal tag = RPMTAG_NOT_FOUND;
    struct json_object *key = NULL;
    rpmTagType type = RPM_NULL_TYPE;

    if (entry == NULL) {
        return RPM_NULL_TYPE;
    }

    if (json_object_object_get_ex(entry, RPM_ENTRY_TYPE_DESC, &key)) {
        type = tag_type(key);
    }

    /*
     * The digest algorithms are written by name and the build time as
     * a timestamp in header.json, so all of those carry the string type
     * there, but all of them are an int32 in the header itself.
     */
    if (type == RPM_STRING_TYPE && json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &key)) {
        /*
         * Go by the tag number rather than the name.  rpm renamed the
         * payload digest algorithm tag in 6.0.0 and get_tag_number()
         * takes either spelling.
         */
        tag = get_tag_number(entry, false);

        if (tag == RPMTAG_FILEDIGESTALGO || tag == RPMTAG_PAYLOAD_DIGEST_ALGO || tag == RPMTAG_BUILDTIME) {
            type = RPM_INT32_TYPE;
        }
    }

    return type;
}

/* Helper to check if a tag is read-only */
static bool
is_read_only_tag(struct json_object *entry)
{
    struct json_object *readonly = NULL;

    if (entry == NULL) {
        return false;
    }

    if (json_object_object_get_ex(entry, RPM_METADATA_READ_ONLY, &readonly)) {
        return true;
    }

    return false;
}

/*
 * Helper to check if a tag should be left out of the header we are
 * building.  Signature tags are extracted for informational purposes,
 * but are regenerated when creating an RPM.
 *
 * There are sort of two kinds of read-only tags that we display from
 * RPM.  The first are ones that are extracted but then ignored when
 * creating an RPM.  That category contains all of the tags handled by
 * rpmsign(1).  The other kind are tags that we extract for informational
 * purposes but recalculate when creating a package.  That includes all
 * of the tags in the signature header and the lead.
 */
static bool
is_skipped_tag(struct json_object *entry, bool is_signature)
{
    rpmTagVal tag = 0;

    if (is_signature) {
        tag = get_tag_number(entry, true);

        /* the file and verity signatures come from header.json */
        if (is_file_signature_tag(tag)) {
            return false;
        }

        /* with -k the package signatures from signature.json stay */
        if (keep_signatures) {
            return false;
        }

        return is_rpmsign_tag(tag);
    }

    return is_read_only_tag(entry);
}

/*
 * The verity algorithm lives in header.json with the verity
 * signatures the "files" array holds, but rpm(8) rejects a main
 * header that carries either one.  Both belong in the signature
 * header.
 */
static void
drop_verity_tags(struct json_object *tags)
{
    const rpmTagVal verity[] = { RPMSIGTAG_VERITYSIGNATURES_VALUE, RPMSIGTAG_VERITYSIGNATUREALGO_VALUE };

    drop_tags(tags, verity, sizeof(verity) / sizeof(verity[0]));
    return;
}

/* Free the file contents collected by read_tag_files(). */
static void
free_tag_files(struct tagfile *tagfiles, size_t len)
{
    size_t i = 0;

    if (tagfiles == NULL) {
        return;
    }

    for (i = 0; i < len; i++) {
        free(tagfiles[i].data);
    }

    free(tagfiles);
    return;
}

/*
 * Read the file holding the value of a file-backed tag.  On success
 * returns 0 with the contents in *data and the byte length, not
 * counting the trailing NUL, in *len.  Returns -1 if the file cannot
 * be read.
 */
static int
read_tag_file(const char *path, uint8_t **data, size_t *len)
{
    off_t filelen = 0;
    struct stat sb;

    if (path == NULL || data == NULL || len == NULL) {
        return -1;
    }

    if (stat(path, &sb) == -1) {
        warn_escaped(_("*** unable to read %s"), path);
        return -1;
    }

    if (!S_ISREG(sb.st_mode)) {
        warnx_escaped(_("*** %s is not a regular file"), path);
        return -1;
    }

    /*
     * read_file_bytes() reports an empty file the same way it
     * reports a failure, so we handle empty here.  The tag value is
     * the empty string, which still takes the one byte of its NUL.
     */
    if (sb.st_size == 0) {
        *data = xalloc(1);
        *len = 0;
        return 0;
    }

    *data = read_file_bytes(path, &filelen);

    if (*data == NULL) {
        warn_escaped(_("*** unable to read %s"), path);
        return -1;
    }

    /* the byte length, not strlen() */
    *len = filelen;

    /*
     * rpm counts on a string ending at the first NUL, so we cannot
     * keep a NUL inside one.  Catch it here, tell the user and stop.
     */
    if (memchr(*data, '\0', *len) != NULL) {
        warnx_escaped(_("*** %s contains a NUL byte and cannot be used as a tag value"), path);
        free(*data);
        *data = NULL;
        *len = 0;
        return -1;
    }

    return 0;
}

/*
 * Returns true if path is below base and nothing on the way from base
 * to it is a symlink.  path has to be absolute with no "." or ".."
 * left in it.
 */
static bool
tag_file_below(const char *path, const char *base)
{
    size_t len = 0;

    if (path == NULL || base == NULL) {
        return false;
    }

    len = strlen(base);

    while (len > 0 && base[len - 1] == '/') {
        len--;
    }

    if (strncmp(path, base, len) || path[len] != '/') {
        return false;
    }

    return path_beneath(base, path + len, false);
}

/*
 * Return the key an entry keeps its value under.  A file backed tag
 * names the file holding its value, but a hand written header.json
 * can carry that value inline instead, so fall back to the value key.
 * NOTE: Do not free() what this function returns.
 */
static const char *
entry_value_key(struct json_object *entry, const rpmTagVal tag)
{
    if (is_file_tag(tag) && json_object_object_get_ex(entry, RPM_ENTRY_FILE_DESC, NULL)) {
        return RPM_ENTRY_FILE_DESC;
    }

    return RPM_ENTRY_VALUE_DESC;
}

/*
 * Read the file a tag names.  A relative filename is taken from
 * tagfile_dir, which is where header.json sits.  The file has to be
 * below tagfile_dir or below payload_dir, which can be anywhere.
 * Returns 0 on success, -1 if the file could not be read.
 */
static int
read_named_tag_file(const rpmTagVal tag_number, const char *name, uint8_t **data, size_t *len, const char *tagfile_dir, const char *payload_dir)
{
    char *full = NULL;
    char *path = NULL;

    if (name[0] == '/') {
        full = xstrdup(name);
    } else if (tagfile_dir != NULL) {
        full = joinpath(tagfile_dir, name, NULL);
    }

    /*
     * We resolve ".." ourselves and open what that gives us, so a
     * crafted header.json cannot pull in a file from anywhere but
     * the two directories that belong to the package.
     */
    path = abspath(full);
    free(full);

    if (!tag_file_below(path, tagfile_dir) && !tag_file_below(path, payload_dir)) {
        warnx_escaped(_("*** the %s tag names \"%s\", which is not a file below the directory holding %s or the payload directory"), rpmTagGetName(tag_number), name, OUTPUT_HEADER);
        free(path);
        return -1;
    }

    if (read_tag_file(path, data, len) == -1) {
        warnx(_("*** unable to read the value of the %s tag"), rpmTagGetName(tag_number));
        free(path);
        return -1;
    }

    free(path);
    return 0;
}

/* Return the Headeri18ntable array in the tags array, NULL if none */
static struct json_object *
i18n_table(struct json_object *tags)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (get_tag_number(entry, false) == HEADER_I18NTABLE && json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value) && json_object_get_type(value) == json_type_array) {
            return value;
        }
    }

    return NULL;
}

/* True if a locale is one the Headeri18ntable array holds at index */
static bool
i18n_table_has(struct json_object *table, const size_t index, const char *locale)
{
    struct json_object *element = NULL;

    element = json_object_array_get_idx(table, index);

    return json_object_get_type(element) == json_type_string && !strcmp(json_object_get_string(element), locale);
}

/*
 * Gather the translations of an i18n string tag.  key is an object of
 * locale to string, or to the name of a file holding the string.  The
 * strings go in the order of Headeri18ntable, one after another, which
 * is how they sit in the header.  A locale in the table with no string
 * here gets an empty one if a later locale has a string.  Returns 0 on
 * success, -1 on failure.
 */
static int
read_i18n_strings(struct json_object *tags, const rpmTagVal tag_number, struct json_object *key, const bool from_files, struct tagfile *tagfile, const char *tagfile_dir, const char *payload_dir)
{
    size_t i = 0;
    size_t len = 0;
    size_t total = 0;
    size_t count = 0;
    bool found = false;
    uint8_t *buf = NULL;
    uint8_t *data = NULL;
    const char *locale = NULL;
    const char *name = NULL;
    struct json_object *table = NULL;
    struct json_object *string = NULL;

    name = rpmTagGetName(tag_number);
    table = i18n_table(tags);

    if (table == NULL) {
        warnx(_("*** the %s tag has translations but the header has no %s tag"), name, rpmTagGetName(RPMTAG_HEADERI18NTABLE));
        return -1;
    }

    json_object_object_foreach(key, keyname, keyvalue) {
        if (json_object_get_type(keyvalue) != json_type_string) {
            warnx(_("*** the %s tag has a value for locale \"%s\" that is not a string"), name, keyname);
            return -1;
        }

        found = false;

        for (i = 0; i < json_object_array_length(table); i++) {
            if (i18n_table_has(table, i, keyname)) {
                found = true;

                if (i + 1 > count) {
                    count = i + 1;
                }
            }
        }

        if (!found) {
            warnx(_("*** the %s tag names the locale \"%s\", which is not in %s"), name, keyname, rpmTagGetName(RPMTAG_HEADERI18NTABLE));
            return -1;
        }
    }

    if (count == 0) {
        warnx(_("*** the %s tag has no strings"), name);
        return -1;
    }

    for (i = 0; i < count; i++) {
        data = NULL;
        len = 0;
        locale = json_object_get_string(json_object_array_get_idx(table, i));

        if (json_object_get_type(json_object_array_get_idx(table, i)) == json_type_string && json_object_object_get_ex(key, locale, &string)) {
            if (from_files) {
                if (read_named_tag_file(tag_number, json_object_get_string(string), &data, &len, tagfile_dir, payload_dir) == -1) {
                    free(buf);
                    return -1;
                }
            } else {
                data = (uint8_t *) xstrdup(json_object_get_string(string));
                len = strlen((const char *) data);
            }
        }

        buf = xrealloc(buf, total + len + 1);

        if (len > 0) {
            memcpy(buf + total, data, len);
        }

        buf[total + len] = '\0';
        total += len + 1;
        free(data);
    }

    tagfile->data = buf;
    tagfile->len = total - 1;
    tagfile->count = count;
    tagfile->present = true;

    return 0;
}

/*
 * Read every file named by a file-backed tag so the sizing pass and
 * the write pass work from the same bytes.  The translations of an
 * i18n string tag go through here too.  Results are indexed by
 * position in the tags array.  Returns 0 on success, -1 if any named
 * file could not be read.
 */
static int
read_tag_files(struct json_object *tags, struct tagfile *tagfiles, const char *tagfile_dir, const char *payload_dir, bool is_signature)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    rpmTagVal tag_number = 0;
    const char *name = NULL;
    const char *field = NULL;

    if (tags == NULL || tagfiles == NULL) {
        return -1;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (is_skipped_tag(entry, is_signature)) {
            continue;
        }

        tag_number = get_tag_number(entry, is_signature);
        field = entry_value_key(entry, tag_number);

        if (get_entry_type(entry) == RPM_I18NSTRING_TYPE && json_object_object_get_ex(entry, field, &key) && json_object_get_type(key) == json_type_object) {
            if (read_i18n_strings(tags, tag_number, key, !strcmp(field, RPM_ENTRY_FILE_DESC), &tagfiles[i], tagfile_dir, payload_dir) == -1) {
                return -1;
            }

            continue;
        }

        if (!is_file_tag(tag_number)) {
            continue;
        }

        if (!json_object_object_get_ex(entry, RPM_ENTRY_FILE_DESC, &key)) {
            continue;
        }

        name = json_object_get_string(key);

        if (name == NULL) {
            continue;
        }

        if (read_named_tag_file(tag_number, name, &tagfiles[i].data, &tagfiles[i].len, tagfile_dir, payload_dir) == -1) {
            return -1;
        }

        tagfiles[i].count = 1;
        tagfiles[i].present = true;
    }

    return 0;
}

/* The name to report a tag by in the header we are building */
static const char *
report_tag_name(const rpmTagVal tag, bool is_signature)
{
    if (is_signature) {
        return sig_tag_name(tag);
    }

    return rpmTagGetName(tag);
}

/*
 * True if every element of an array has the given JSON type.
 */
static bool
array_of(struct json_object *value, const enum json_type type)
{
    size_t i = 0;

    if (json_object_get_type(value) != json_type_array) {
        return false;
    }

    for (i = 0; i < json_object_array_length(value); i++) {
        if (json_object_get_type(json_object_array_get_idx(value, i)) != type) {
            return false;
        }
    }

    return true;
}

/*
 * True if a JSON value is a number.  We write the signature sizes as
 * strings of digits and json-c reads those back as numbers, so a
 * string that holds a whole number counts too.
 */
static bool
is_number(struct json_object *value)
{
    const char *s = NULL;
    char *end = NULL;

    if (json_object_get_type(value) == json_type_int) {
        return true;
    }

    if (json_object_get_type(value) != json_type_string) {
        return false;
    }

    s = json_object_get_string(value);

    if (*s == '\0') {
        return false;
    }

    errno = 0;
    (void) strtoll(s, &end, 10);

    if (errno != 0 || *end != '\0') {
        return false;
    }

    return true;
}

/*
 * True if a number tag holds a number or an array of numbers.  The
 * digest algorithms and the build time may be written by name or as
 * a timestamp instead.
 */
static bool
valid_number_value(const rpmTagType type, const rpmTagVal tag, struct json_object *value)
{
    size_t i = 0;

    if (is_number(value)) {
        return true;
    }

    if (type == RPM_INT32_TYPE && json_object_get_type(value) == json_type_string) {
        if (tag == RPMTAG_FILEDIGESTALGO || tag == RPMTAG_PAYLOAD_DIGEST_ALGO || tag == RPMTAG_BUILDTIME) {
            return true;
        }
    }

    if (json_object_get_type(value) != json_type_array) {
        return false;
    }

    for (i = 0; i < json_object_array_length(value); i++) {
        if (!is_number(json_object_array_get_idx(value, i))) {
            return false;
        }
    }

    return true;
}

/*
 * True if a number fits in an int8, int16 or int32 tag.  rpm reads
 * these unsigned and put_number() writes a negative value as the
 * same bits, so a type takes its signed low end up to its unsigned
 * high end.  A name the lookups turn in to a number always fits, and
 * so does any int64.
 */
static bool
number_fits(const rpmTagType type, struct json_object *value)
{
    int64_t n = 0;
    int64_t low = 0;
    int64_t high = 0;

    if (!is_number(value)) {
        return true;
    }

    if (type == RPM_INT8_TYPE) {
        low = INT8_MIN;
        high = UINT8_MAX;
    } else if (type == RPM_INT16_TYPE) {
        low = INT16_MIN;
        high = UINT16_MAX;
    } else if (type == RPM_INT32_TYPE) {
        low = INT32_MIN;
        high = UINT32_MAX;
    } else {
        return true;
    }

    /* json-c gives back INT64_MAX for a number past it, which is still too big */
    if (json_object_get_type(value) == json_type_string) {
        n = strtoll(json_object_get_string(value), NULL, 10);
    } else {
        n = json_object_get_int64(value);
    }

    return (n >= low && n <= high);
}

/* True if every number a tag holds fits in its type */
static bool
valid_number_range(const rpmTagType type, struct json_object *value)
{
    size_t i = 0;

    if (json_object_get_type(value) != json_type_array) {
        return number_fits(type, value);
    }

    for (i = 0; i < json_object_array_length(value); i++) {
        if (!number_fits(type, json_object_array_get_idx(value, i))) {
            return false;
        }
    }

    return true;
}

/* True if a binary tag holds a base64 string */
static bool
valid_binary_value(struct json_object *value)
{
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    bool r = false;

    if (json_object_get_type(value) != json_type_string) {
        return false;
    }

    if (rpmBase64Decode(json_object_get_string(value), (void **) &blob, &blobsize) == 0) {
        r = true;
    }

    free(blob);
    return r;
}

/*
 * Make sure each tag value has the JSON type its RPM header type
 * needs before we size and write the header.  A string is a JSON
 * string and a string array is an array of them.  A number type is a
 * number or an array of numbers.  A binary blob is a base64 string.
 * A char is a string of at least one character.  tag_type() gives
 * RPM_NULL_TYPE for "(null)", "(unknown)" and any name it does not
 * know, and we refuse those.  The region tags have their own check in
 * check_region_tags().
 *
 * Returns 0 on success, -1 if any value has the wrong type.
 */
static int
check_tag_values(struct json_object *tags, const struct tagfile *tagfiles, bool is_signature)
{
    int r = 0;
    size_t i = 0;
    rpmTagType entry_type = RPM_NULL_TYPE;
    rpmTagVal tag_number = 0;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    const char *name = NULL;

    if (tags == NULL || tagfiles == NULL) {
        return -1;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (is_skipped_tag(entry, is_signature) || tagfiles[i].present) {
            continue;
        }

        entry_type = get_entry_type(entry);
        tag_number = get_tag_number(entry, is_signature);

        if (tag_number == HEADER_SIGNATURES || tag_number == HEADER_IMMUTABLE) {
            continue;
        }

        if (!json_object_object_get_ex(entry, entry_value_key(entry, tag_number), &key)) {
            continue;
        }

        name = report_tag_name(tag_number, is_signature);

        /* rpm turns away a tag with a count of zero */
        if (json_object_get_type(key) == json_type_array && json_object_array_length(key) == 0) {
            warnx(_("*** the %s tag has an empty array as its value, rpm needs at least one value"), name);
            r = -1;
            continue;
        }

        if (entry_type == RPM_STRING_TYPE || entry_type == RPM_I18NSTRING_TYPE) {
            if (json_object_get_type(key) != json_type_string) {
                warnx(_("*** the %s tag has a string type but its value is not a string"), name);
                r = -1;
            }
        } else if (entry_type == RPM_STRING_ARRAY_TYPE) {
            if (!array_of(key, json_type_string)) {
                warnx(_("*** the %s tag has a string array type but its value is not an array of strings"), name);
                r = -1;
            }
        } else if (entry_type == RPM_INT8_TYPE || entry_type == RPM_INT16_TYPE || entry_type == RPM_INT32_TYPE || entry_type == RPM_INT64_TYPE) {
            if (!valid_number_value(entry_type, tag_number, key)) {
                warnx(_("*** the %s tag has a number type but its value is not a number or an array of numbers"), name);
                r = -1;
            } else if (!valid_number_range(entry_type, key)) {
                warnx(_("*** the %s tag has a value that does not fit in its %s type"), name, strtagtype(entry_type));
                r = -1;
            }
        } else if (entry_type == RPM_BIN_TYPE) {
            if (!valid_binary_value(key)) {
                warnx(_("*** the %s tag has a binary blob type but its value is not base64 data"), name);
                r = -1;
            }
        } else if (entry_type == RPM_CHAR_TYPE) {
            if (json_object_get_type(key) != json_type_string || json_object_get_string_len(key) == 0) {
                warnx(_("*** the %s tag has a char type but its value is not a string of one or more characters"), name);
                r = -1;
            }
        } else {
            warnx(_("*** the %s tag has no type tarpm can write"), name);
            r = -1;
        }
    }

    return r;
}

/*
 * Every tag we write needs a number.  get_tag_number() gives back
 * RPMTAG_NOT_FOUND for a name it does not know, which would go in to
 * the header as a tag rpm rejects.
 *
 * Returns 0 on success, -1 if any tag name is unknown.
 */
static int
check_tag_numbers(struct json_object *tags, bool is_signature)
{
    int r = 0;
    size_t i = 0;
    rpmTagVal tag_number = 0;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    const char *name = NULL;

    if (tags == NULL) {
        return -1;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (is_skipped_tag(entry, is_signature)) {
            continue;
        }

        tag_number = get_tag_number(entry, is_signature);

        if (tag_number != RPMTAG_NOT_FOUND && tag_number != 0) {
            continue;
        }

        name = NULL;

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &key)) {
            name = json_object_get_string(key);
        }

        if (name == NULL) {
            name = "";
        }

        warnx(_("*** unknown tag \"%s\" in header data"), name);
        r = -1;
    }

    return r;
}

/*
 * The region tag value is the trailer itself, so it has to be binary
 * data that decodes to exactly one trailer.
 */
static bool
valid_region_value(struct json_object *entry, const rpmTagVal tag_number)
{
    struct json_object *key = NULL;
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    bool r = false;

    if (get_entry_type(entry) != RPM_BIN_TYPE) {
        return false;
    }

    if (!json_object_object_get_ex(entry, entry_value_key(entry, tag_number), &key) || json_object_get_type(key) != json_type_string) {
        return false;
    }

    if (rpmBase64Decode(json_object_get_string(key), (void **) &blob, &blobsize) == 0 && blobsize == RPM_TRAILER_SIZE) {
        r = true;
    }

    free(blob);
    return r;
}

/*
 * The region tag carries the trailer and the header sizing only makes
 * room for one.  A header carries at most one region tag and it has
 * to be the one for this kind of header, which is
 * Headersignatures for the signature header and Headerimmutable for
 * the main header.
 *
 * Returns 0 on success, -1 if the region tags are wrong.
 */
static int
check_region_tags(struct json_object *tags, bool is_signature)
{
    int r = 0;
    size_t i = 0;
    size_t nregions = 0;
    rpmTagVal tag_number = 0;
    rpmTagVal region = HEADER_IMMUTABLE;
    struct json_object *entry = NULL;

    if (tags == NULL) {
        return -1;
    }

    if (is_signature) {
        region = HEADER_SIGNATURES;
    } else {
        region = HEADER_IMMUTABLE;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (is_skipped_tag(entry, is_signature)) {
            continue;
        }

        tag_number = get_tag_number(entry, is_signature);

        if (tag_number != HEADER_SIGNATURES && tag_number != HEADER_IMMUTABLE) {
            continue;
        }

        if (tag_number != region) {
            /* librpm has no name for Headersignatures, so use ours */
            warnx(_("*** the %s tag does not belong in this header, use %s"), sig_tag_name(tag_number), sig_tag_name(region));
            r = -1;
        }

        if (!valid_region_value(entry, tag_number)) {
            warnx(_("*** the %s tag must have type binary blob and a value of %d bytes"), sig_tag_name(tag_number), RPM_TRAILER_SIZE);
            r = -1;
        }

        nregions++;
    }

    if (nregions > 1) {
        warnx(_("*** header data carries %zu region tags, only one is allowed"), nregions);
        r = -1;
    }

    return r;
}

/*
 * How many bytes of padding a type needs at this point in the data
 * area.  rpm lines the numbers up on their own size.
 */
static int32_t
align_padding(const rpmTagType type, const size_t offset)
{
    if (type == RPM_INT16_TYPE) {
        return (2 - (offset % 2)) % 2;
    }

    if (type == RPM_INT32_TYPE) {
        return (4 - (offset % 4)) % 4;
    }

    if (type == RPM_INT64_TYPE) {
        return (8 - (offset % 8)) % 8;
    }

    return 0;
}

static size_t
get_item_size(size_t index, struct json_object *entry, const struct tagfile *tagfiles, int32_t *trailer_index, size_t *trailer_size, bool is_signature)
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

    if (entry == NULL) {
        return 0;
    }

    entry_type = get_entry_type(entry);
    tag_number = get_tag_number(entry, is_signature);

    /* get the value field from the entry */
    if (!json_object_object_get_ex(entry, entry_value_key(entry, tag_number), &key)) {
        return 0;
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
    } else if (entry_type == RPM_CHAR_TYPE) {
        /* one byte per character and no NUL */
        item_size = strlen(json_object_get_string(key));
    } else if (entry_type == RPM_STRING_ARRAY_TYPE) {
        /* string array: sum of all string lengths + NULs */
        len = json_object_array_length(key);

        for (j = 0; j < len; j++) {
            s = json_object_array_get_idx(key, j);
            item_size += strlen(json_object_get_string(s)) + 1;
        }
    } else {
        /* string data: length + NUL */
        if (tagfiles[index].present) {
            /*
             * The file was read before the header was sized so we
             * already know the size of this tag.
             */
            item_size = tagfiles[index].len + 1;
        } else {
            value = json_object_get_string(key);

            if (value == NULL) {
                item_size = 0;
            } else {
                item_size = strlen(value) + 1;
            }
        }
    }

    return item_size;
}

/* Calculate the size of the data buffer for this header. */
static size_t
get_data_buffer_size(struct json_object *tags, const struct tagfile *tagfiles, int32_t *trailer_index, size_t *trailer_size, bool is_signature)
{
    size_t datasize = 0;
    size_t i = 0;
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

        /* skip read-only tags and the ones rpmsign owns */
        if (is_skipped_tag(entry, is_signature)) {
            continue;
        }

        /* get the tag number and type from this entry */
        tag_number = get_tag_number(entry, is_signature);
        entry_type = get_entry_type(entry);

        /* get the value and calculate size */
        if (json_object_object_get_ex(entry, entry_value_key(entry, tag_number), &key)) {
            item_size = get_item_size(i, entry, tagfiles, trailer_index, trailer_size, is_signature);

            /* sequential calculation with alignment */
            padding = align_padding(entry_type, datasize);
            datasize += padding + item_size;
        }
    }

    return datasize;
}

/*
 * Move the writer along to where the next value lines up.
 */
static void
pad_writer(struct data_writer *w, const rpmTagType type)
{
    int32_t padding = 0;

    padding = align_padding(type, w->offset);
    w->offset += padding;
    w->datapos += padding;

    return;
}

/*
 * Give back the number a JSON value holds for an int32 tag.  The
 * digest algorithms are written by name in header.json and the build
 * time as a timestamp, so those three need a lookup first.
 */
static uint32_t
int32_value(const rpmTagVal tag, struct json_object *value)
{
    if (json_object_get_type(value) == json_type_string) {
        if (tag == RPMTAG_FILEDIGESTALGO || tag == RPMTAG_PAYLOAD_DIGEST_ALGO) {
            return digest_algo(json_object_get_string(value));
        }

        if (tag == RPMTAG_BUILDTIME) {
            return buildtime_value(json_object_get_string(value));
        }
    }

    return (uint32_t) json_object_get_int64(value);
}

/*
 * Write one number in to the data area in network byte order and move
 * the writer along.
 */
static void
put_number(struct data_writer *w, const rpmTagType type, const rpmTagVal tag, struct json_object *value)
{
    size_t len = 0;
    union datatypes n;

    if (type == RPM_INT8_TYPE) {
        n.i8 = json_object_get_int(value);
        len = sizeof(n.i8);
        memcpy(w->datapos, &n.i8, len);
    } else if (type == RPM_INT16_TYPE) {
        /* a negative value is the same 16 bits rpm reads unsigned */
        n.i16 = htons((uint16_t) json_object_get_int64(value));
        len = sizeof(n.i16);
        memcpy(w->datapos, &n.i16, len);
    } else if (type == RPM_INT32_TYPE) {
        n.i32 = htonl(int32_value(tag, value));
        len = sizeof(n.i32);
        memcpy(w->datapos, &n.i32, len);
    } else {
        n.i64 = htobe64((uint64_t) json_object_get_int64(value));
        len = sizeof(n.i64);
        memcpy(w->datapos, &n.i64, len);
    }

    w->datapos += len;
    w->offset += len;

    return;
}

/*
 * Write the numbers a tag carries in to the data area and give back
 * how many went in.  A tag holds one number or an array of them.
 */
static uint32_t
write_numbers(struct data_writer *w, const rpmTagType type, const rpmTagVal tag, struct json_object *key)
{
    uint32_t i = 0;
    uint32_t count = 0;

    if (json_object_get_type(key) != json_type_array) {
        put_number(w, type, tag, key);
        return 1;
    }

    count = json_object_array_length(key);

    /*
     * The three tags we write by name are single values, so the ones
     * in an array all go in as plain numbers.
     */
    for (i = 0; i < count; i++) {
        put_number(w, type, 0, json_object_array_get_idx(key, i));
    }

    return count;
}

/*
 * Write one string and its NUL in to the data area and move the
 * writer along.
 */
static void
put_string(struct data_writer *w, const char *str, const size_t len)
{
    memcpy(w->datapos, str, len + 1);
    w->datapos += len + 1;
    w->offset += len + 1;

    return;
}

/*
 * Write the strings a tag carries in to the data area, each one with
 * its NUL, and give back how many went in.
 */
static uint32_t
write_strings(struct data_writer *w, struct json_object *key)
{
    uint32_t i = 0;
    uint32_t count = 0;
    const char *str = NULL;

    count = json_object_array_length(key);

    for (i = 0; i < count; i++) {
        str = json_object_get_string(json_object_array_get_idx(key, i));
        put_string(w, str, strlen(str));
    }

    return count;
}

/*
 * Write the characters of a char tag without a NUL and give back how
 * many went in.
 */
static uint32_t
write_chars(struct data_writer *w, struct json_object *key)
{
    size_t len = 0;

    len = strlen(json_object_get_string(key));
    memcpy(w->datapos, json_object_get_string(key), len);
    w->datapos += len;
    w->offset += len;

    return (uint32_t) len;
}

/*
 * Write the single string a tag carries.  A file backed tag gets the
 * bytes we read before the header was sized, which is what
 * get_item_size() counted.  Everything else writes the value the
 * entry carries inline.
 */
static void
write_string(struct data_writer *w, const struct tagfile *tagfile, struct json_object *key)
{
    const char *str = NULL;

    if (tagfile->present) {
        put_string(w, (const char *) tagfile->data, tagfile->len);
        return;
    }

    str = json_object_get_string(key);

    if (str == NULL) {
        str = "";
    }

    put_string(w, str, strlen(str));

    return;
}

/*
 * Write the base64 data a tag carries in to the data area.  Returns 0
 * if it worked and -1 if we could not decode it.
 */
static int
write_binary(struct data_writer *w, struct rpmhdrentry *entry, struct json_object *key)
{
    int b = 0;
    uint8_t *blob = NULL;
    size_t blobsize = 0;

    b = rpmBase64Decode(json_object_get_string(key), (void **) &blob, &blobsize);
    entry->count = (uint32_t) blobsize;

    if (b != 0) {
        warnx(_("*** rpmBase64Decode failed with code %d"), b);
        return -1;
    }

    memcpy(w->datapos, blob, entry->count);
    w->datapos += entry->count;
    w->offset += entry->count;
    free(blob);

    return 0;
}

/*
 * Write the value of one tag in to the data area and record how many
 * items went in.  Returns 0 if it worked and -1 if it did not.
 */
static int
write_entry_value(struct data_writer *w, struct rpmhdrentry *entry, const struct tagfile *tagfile, struct json_object *key)
{
    if (entry->type == RPM_BIN_TYPE) {
        return write_binary(w, entry, key);
    }

    if (entry->type == RPM_INT8_TYPE || entry->type == RPM_INT16_TYPE || entry->type == RPM_INT32_TYPE || entry->type == RPM_INT64_TYPE) {
        entry->count = write_numbers(w, entry->type, entry->tag, key);
    } else if (entry->type == RPM_STRING_ARRAY_TYPE) {
        entry->count = write_strings(w, key);
    } else if (entry->type == RPM_CHAR_TYPE) {
        entry->count = write_chars(w, key);
    } else {
        entry->count = 1;

        if (tagfile->present) {
            entry->count = tagfile->count;
        }

        write_string(w, tagfile, key);
    }

    return 0;
}

/* Add the header tags and their values to the data buffer */
static int
add_header_tags(struct json_object *tags, const struct tagfile *tagfiles, struct rpmhdrinfo *v, size_t totalsize, int32_t trailer_index, size_t trailer_size, bool is_signature)
{
    int r = 0;
    size_t i = 0;
    const char *field = NULL;
    struct data_writer w;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;

    if (tags == NULL || tagfiles == NULL || v == NULL) {
        return -1;
    }

    /* we fill the data area in from the front */
    w.datapos = v->datastart;
    w.offset = 0;

    /* create header tags and copy in the values */
    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        /* skip read-only tags and the ones rpmsign owns */
        if (is_skipped_tag(entry, is_signature)) {
            continue;
        }

        v->entry->tag = get_tag_number(entry, is_signature);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TYPE_DESC, &key) == 0) {
            warnx(_("*** invalid header tag entry, missing 'type'"));
            r = -1;
        } else {
            v->entry->type = get_entry_type(entry);
        }

        if (trailer_index >= 0 && i == ((size_t) trailer_index)) {
            /*
             * The trailer does not go in to the data area, so its
             * offset points just past the end of what does.
             */
            v->entry->offset = totalsize;
            v->entry->count = trailer_size;
        } else {
            pad_writer(&w, v->entry->type);
            v->entry->offset = w.offset;

            field = entry_value_key(entry, v->entry->tag);

            if (json_object_object_get_ex(entry, field, &key) == 0) {
                warnx(_("*** invalid header tag entry, missing '%s'"), field);
                r = -1;
            } else if (write_entry_value(&w, v->entry, &tagfiles[i], key) == -1) {
                r = -1;
            }
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
read_header(const int fd, const char *dest_dir, bool *raw_tags)
{
    uint32_t *buffer = NULL;
    struct rpmhdr *rawhdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    struct rpmhdrentry *trailer = NULL;
    struct json_object *jvals = NULL;
    struct json_object *header = NULL;
    struct json_object *changelog = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *files = NULL;

    if (fd < 0 || raw_tags == NULL) {
        return NULL;
    }

    *raw_tags = false;

    /* read in the header signature -- identifies the start of an RPM header block */
    rawhdr = read_header_signature(fd);

    if (rawhdr == NULL) {
        warnx(_("*** unable to read the main header"));
        return NULL;
    }

    /* computed from header values */
    hdrinfo = mkhdrinfo(rawhdr, false);

    if (hdrinfo == NULL) {
        free(rawhdr);
        return NULL;
    }

    /* read in the entries */
    buffer = read_header_entries(fd, rawhdr, hdrinfo->hlen);

    if (buffer == NULL) {
        warnx(_("*** the main header is cut short"));
        free(hdrinfo);
        free(rawhdr);
        return NULL;
    }

    hdrinfo->estart = (struct rpmhdrentry *) &(buffer[2]);
    hdrinfo->datastart = (uint8_t *) (hdrinfo->estart + rawhdr->nentries);

    if (!valid_header_entries(rawhdr, hdrinfo->estart, hdrinfo->datastart)) {
        free(buffer);
        free(hdrinfo);
        free(rawhdr);
        return NULL;
    }

    /* handle trailer */
    /* the trailer is not guaranteed to be aligned, copy required */
    trailer = read_header_trailer(rawhdr, hdrinfo->estart, hdrinfo->datastart);

    /* generate a JSON structure for the signature */
    header = generate_json(rawhdr);

    /* dump all of the tags in the header block */
    jvals = generate_json_entries(rawhdr, hdrinfo, trailer, dest_dir, false);

    if (jvals == NULL) {
        json_object_put(header);
        free(trailer);
        free(buffer);
        free(hdrinfo);
        free(rawhdr);
        return NULL;
    }

    /* write the header to a file */
    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, json_object_get(jvals));

    /* build the dependencies object if dependency tags are present */
    dependencies = generate_dependencies(rawhdr, hdrinfo);

    if (dependencies != NULL) {
        json_object_object_add(header, RPM_DEPENDENCIES_DESC, dependencies);
    }

    /*
     * Build the files array if file list tags are present.  This has
     * to come after the dependencies because the depends dictionary
     * carried by each file is expressed in terms of them.
     */
    files = generate_files(rawhdr, hdrinfo, dependencies);

    if (files != NULL) {
        json_object_object_add(header, RPM_FILES_DESC, files);
    } else if (keep_raw_entries(jvals, rawhdr, hdrinfo, dest_dir, is_file_list_tag)) {
        warnx(_("*** unable to build the %s array, keeping the raw tags in %s"), RPM_FILES_DESC, OUTPUT_HEADER);
        *raw_tags = true;
    }

    /* build the changelog array if changelog tags are present */
    changelog = generate_changelog(rawhdr, hdrinfo);

    if (changelog != NULL) {
        json_object_object_add(header, RPM_CHANGELOG_DESC, changelog);
    } else if (keep_raw_entries(jvals, rawhdr, hdrinfo, dest_dir, is_changelog_tag)) {
        warnx(_("*** unable to build the %s array, keeping the raw tags in %s"), RPM_CHANGELOG_DESC, OUTPUT_HEADER);
        *raw_tags = true;
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
 * Create a new header to write to an RPM file later.  A header
 * starts with the magic number, the record count and the size of the
 * storage area.  The storage area is a run of header index structs
 * followed by the data those records point at.  This mostly sets the
 * header up and the caller adds records to it after.
 *
 * Returns 0 on success, non-zero otherwise.  The caller passes in
 * pointers for the rpmhdr and the rpmhdrinfo, which we allocate and
 * fill in.  Caller must free both.
 */
int
create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo, const char *payload_dir, const char *tagfile_dir, bool is_signature)
{
    int r = 0;
    struct rpmhdr *s;
    struct rpmhdrinfo *v;
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *tags_copy = NULL;
    struct tagfile *tagfiles = NULL;
    size_t ntags = 0;
    size_t totalsize = 0;
    int32_t trailer_index = -1;
    size_t trailer_size = 0;
    size_t i = 0;

    if (data == NULL || hdr == NULL || hdrinfo == NULL) {
        return -1;
    }

    /* the caller gets nothing back unless we succeed */
    *hdr = NULL;
    *hdrinfo = NULL;

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

    /*
     * Work on a copy of the tags array.  We append the generated
     * tags to it and sort it below, and the caller should see
     * neither.
     */
    tags_copy = json_object_new_array();

    for (i = 0; i < json_object_array_length(tags); i++) {
        json_object_array_add(tags_copy, json_object_get(json_object_array_get_idx(tags, i)));
    }

    tags = tags_copy;

    /* Check if there's a changelog array that needs to be converted to tags */
    if (json_object_object_get_ex(data, RPM_CHANGELOG_DESC, &changelog)) {
        if (add_changelog_tags(tags_copy, changelog) == -1) {
            json_object_put(tags);
            free(s);
            free(v);
            return -1;
        }
    }

    /* Check if there's a dependencies object that needs to be converted to tags */
    if (json_object_object_get_ex(data, RPM_DEPENDENCIES_DESC, &dependencies)) {
        add_dependency_tags(tags_copy, dependencies);
    }

    /*
     * Take out the tags the format we are writing does not carry.
     * This runs here rather than earlier because the file list tags
     * we just generated are part of what the format decides.
     */
    if (!is_signature) {
        filter_format_tags(tags_copy, rpmformat);
        drop_verity_tags(tags_copy);
    }

    /*
     * Lay the header out the way rpm would have written it.  rpm sorts
     * the index entries by tag number and lays the data area out in
     * the same order.  See headerSort() and headerExport() in
     * lib/header.cc in the rpm source.
     */
    sort_tags_stable(tags_copy, is_signature);

    /*
     * Read the files backing the file tags up front, so sizing the
     * header and writing it both work from the same bytes.  A file
     * named here that cannot be read is fatal for rpm.
     */
    ntags = json_object_array_length(tags);
    tagfiles = xcalloc(ntags, sizeof(*tagfiles));

    if (check_tag_numbers(tags, is_signature) == -1 || check_region_tags(tags, is_signature) == -1 || read_tag_files(tags, tagfiles, tagfile_dir, payload_dir, is_signature) == -1 || check_tag_values(tags, tagfiles, is_signature) == -1) {
        free_tag_files(tagfiles, ntags);
        json_object_put(tags);
        free(s);
        free(v);
        return -1;
    }

    /* number of header index entries (excluding the tags we skip) */
    s->nentries = 0;

    for (i = 0; i < json_object_array_length(tags); i++) {
        if (!is_skipped_tag(json_object_array_get_idx(tags, i), is_signature)) {
            s->nentries++;
        }
    }

    /* allocate an array for the header index entries */
    v->estart = xcalloc(s->nentries, sizeof(*(v->estart)));
    v->entry = v->estart;
    s->nentries = htonl(s->nentries);

    /*
     * total size is just the sequential data, NOT including trailer
     * trailer offset will point past the end of data
     */
    totalsize = get_data_buffer_size(tags, tagfiles, &trailer_index, &trailer_size, is_signature);

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
    r = add_header_tags(tags, tagfiles, v, totalsize, trailer_index, trailer_size, is_signature);

    /* free our copy of the tags array */
    json_object_put(tags);

    free_tag_files(tagfiles, ntags);

    if (r != 0) {
        free(v->estart);
        free(v->datastart);
        free(v);
        free(s);
        return r;
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
 * Looks for the region tag of this kind of header in the header tags
 * and if found, does a base64 decode and stores that in the
 * trailer_data parameter and the size in the trailer_size parameter.
 * Returns 0 on success, non-zero on error.  Caller must free
 * trailer_data.
 */
int
get_trailer_data(const struct json_object *data, uint8_t **trailer_data, size_t *trailer_size, bool is_signature)
{
    size_t i = 0;
    int r = 0;
    rpmTagVal region = HEADER_IMMUTABLE;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *vobj = NULL;
    const char *trailer_value = NULL;

    if (data == NULL || trailer_data == NULL || trailer_size == NULL) {
        return -1;
    }

    if (is_signature) {
        region = HEADER_SIGNATURES;
    } else {
        region = HEADER_IMMUTABLE;
    }

    if (json_object_object_get_ex(data, RPM_ENTRY_TAGS_DESC, &tags) == 1) {
        for (i = 0; i < json_object_array_length(tags); i++) {
            entry = json_object_array_get_idx(tags, i);

            /* only the region tag that goes in to the header counts */
            if (is_skipped_tag(entry, is_signature) || get_tag_number(entry, is_signature) != region) {
                continue;
            }

            if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &vobj) == 1) {
                trailer_value = json_object_get_string(vobj);
                r = rpmBase64Decode(trailer_value, (void **) trailer_data, trailer_size);

                if (r == 0 && *trailer_size == RPM_TRAILER_SIZE) {
                    return 0;
                }

                free(*trailer_data);
                *trailer_data = NULL;
            }

            break;
        }
    }

    return -1;
}

/*
 * The region trailer records the size of the index entries it covers
 * as a negative offset.  The trailer we read back from the JSON still
 * describes the header the package came with, so we work the offset
 * out again from the entries we really have.  That leaves out
 * read-only tags like the signatures on a signed package and takes in
 * the tags a format change added.  Everything that reads the trailer
 * has to go through here or the digests will not match what we write.
 */
void
fix_trailer_offset(uint8_t *trailer_data, const size_t trailer_size, const uint32_t nentries)
{
    int32_t offset = 0;

    if (trailer_data == NULL || trailer_size != RPM_TRAILER_SIZE) {
        return;
    }

    offset = htonl(-((int32_t) (nentries * sizeof(struct rpmhdrentry))));
    memcpy(trailer_data + (2 * sizeof(int32_t)), &offset, sizeof(offset));

    return;
}
