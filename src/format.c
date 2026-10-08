/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <err.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmpgp.h>

#include "tarpm.h"

/* the format we write the package for */
int rpmformat = RPM_FORMAT_DEFAULT;

/* the main header tags rpm works out at write time, one group per RPM format */
static const rpmTagVal v4_header_tags[] = { RPMFORMAT_V4_HEADER_TAGS };
static const rpmTagVal v6_header_tags[] = { RPMFORMAT_V6_HEADER_TAGS };

#define V4_HEADER_NTAGS (sizeof(v4_header_tags) / sizeof(v4_header_tags[0]))
#define V6_HEADER_NTAGS (sizeof(v6_header_tags) / sizeof(v6_header_tags[0]))

/* the main header tags only one of the two formats carries */
static const rpmTagVal v4_only_tags[] = { RPMFORMAT_V4_ONLY_HEADER_TAGS };
static const rpmTagVal v6_only_tags[] = { RPMFORMAT_V6_ONLY_HEADER_TAGS };

#define V4_ONLY_NTAGS (sizeof(v4_only_tags) / sizeof(v4_only_tags[0]))
#define V6_ONLY_NTAGS (sizeof(v6_only_tags) / sizeof(v6_only_tags[0]))

/*
 * The size tags that come in a 32 bit and a 64 bit spelling.  rpm
 * records sizes under the 32 bit tag for v4 and the 64 bit tag
 * for v6, and a v4 package falls back to the 64 bit tag
 * for a number that will not fit.
 */
static const struct {
    rpmTagVal small;
    rpmTagVal large;
} size_tag_pairs[] = {
    { RPMTAG_SIZE,      RPMTAG_LONGSIZE      },
    { RPMTAG_FILESIZES, RPMTAG_LONGFILESIZES },
    { 0,                0                    }
};

/*
 * The payload digest tags and what each one covers.  A tag marked
 * uncompressed digests the payload with the compression taken back
 * off, which is the ALT half of each pair.
 */
static const struct {
    rpmTagVal tag;
    int type;
    bool uncompressed;
} payload_digests[] = {
    { RPMTAG_PAYLOADSHA256_VALUE,      TARPM_DIGEST_SHA256,   false },
    { RPMTAG_PAYLOADSHA256ALT_VALUE,   TARPM_DIGEST_SHA256,   true  },
    { RPMTAG_PAYLOADSHA512_VALUE,      TARPM_DIGEST_SHA512,   false },
    { RPMTAG_PAYLOADSHA512ALT_VALUE,   TARPM_DIGEST_SHA512,   true  },
    { RPMTAG_PAYLOADSHA3_256_VALUE,    TARPM_DIGEST_SHA3_256, false },
    { RPMTAG_PAYLOADSHA3_256ALT_VALUE, TARPM_DIGEST_SHA3_256, true  },
    { 0,                               0,                     false }
};

/* Return the group of main header tags a format calls for, NULL for a format we cannot write */
static const rpmTagVal *
header_tags(const int format, size_t *ntags)
{
    if (format == RPM_FORMAT_V4) {
        *ntags = V4_HEADER_NTAGS;
        return v4_header_tags;
    }

    if (format == RPM_FORMAT_V6) {
        *ntags = V6_HEADER_NTAGS;
        return v6_header_tags;
    }

    *ntags = 0;

    return NULL;
}

/* Return true if the group holds the tag */
static bool
in_group(const rpmTagVal *group, const size_t ntags, const rpmTagVal tag)
{
    size_t i = 0;

    for (i = 0; i < ntags; i++) {
        if (group[i] == tag) {
            return true;
        }
    }

    return false;
}

/* Return where a tag sits in the tags array, -1 if it is not there */
static int
find_tag(struct json_object *tags, const rpmTagVal tag)
{
    size_t i = 0;
    struct json_object *entry = NULL;

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (entry != NULL && get_tag_number(entry, false) == tag) {
            return i;
        }
    }

    return -1;
}

/* Take every copy of each tag in the group out of the tags array */
static void
drop_tags(struct json_object *tags, const rpmTagVal *group, const size_t ngroup)
{
    size_t i = 0;
    int idx = -1;

    for (i = 0; i < ngroup; i++) {
        idx = find_tag(tags, group[i]);

        while (idx >= 0) {
            json_object_array_del_idx(tags, idx, 1);
            idx = find_tag(tags, group[i]);
        }
    }

    return;
}

/* Record an entry under a different tag and type, keeping its value */
static void
retag(struct json_object *tags, const rpmTagVal from, const rpmTagVal to, const rpmTagType type)
{
    int idx = -1;
    struct json_object *entry = NULL;

    idx = find_tag(tags, from);

    if (idx < 0) {
        return;
    }

    entry = json_object_array_get_idx(tags, idx);
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(to)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));

    return;
}

/* Return true if every number the tag records fits in 32 bits */
static bool
fits_uint32(struct json_object *tags, const rpmTagVal tag)
{
    int idx = -1;
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    idx = find_tag(tags, tag);

    if (idx < 0) {
        return true;
    }

    entry = json_object_array_get_idx(tags, idx);

    if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
        return true;
    }

    if (json_object_get_type(value) != json_type_array) {
        return json_object_get_int64(value) <= (int64_t) UINT32_MAX;
    }

    for (i = 0; i < json_object_array_length(value); i++) {
        if (json_object_get_int64(json_object_array_get_idx(value, i)) > (int64_t) UINT32_MAX) {
            return false;
        }
    }

    return true;
}

/*
 * Settle a size tag on the spelling the format we are writing uses.
 * add_file_list_tags() rebuilds the 32 bit tag from the payload tree,
 * so it is the fresher of the two when a header carries both and the
 * 64 bit one goes.  v4 holds on to the 64 bit tag for a number that
 * will not fit, which is what rpm does as well.
 */
static void
normalize_size_pair(struct json_object *tags, const rpmTagVal small, const rpmTagVal large, const int format)
{
    int idx = -1;
    rpmTagVal have = 0;
    rpmTagVal want = 0;

    if (find_tag(tags, small) >= 0) {
        idx = find_tag(tags, large);

        if (idx >= 0) {
            json_object_array_del_idx(tags, idx, 1);
        }

        have = small;
    } else if (find_tag(tags, large) >= 0) {
        have = large;
    } else {
        return;
    }

    want = (format == RPM_FORMAT_V6) ? large : small;

    if (have == want) {
        return;
    }

    if (want == small && !fits_uint32(tags, large)) {
        return;
    }

    retag(tags, have, want, (want == large) ? RPM_INT64_TYPE : RPM_INT32_TYPE);

    return;
}

/*
 * Put the tags array in to the shape the format calls for.  A package
 * we extracted carries the tags the format it was built as owns, and
 * leaving those behind would describe the package we write wrongly,
 * so they come out here.  The size tags move over to the spelling the
 * format uses rather than being thrown away, since the numbers are
 * the same either way.  Doing this twice over the same array changes
 * nothing the second time round.
 */
void
filter_format_tags(struct json_object *tags, const int format)
{
    size_t i = 0;

    if (tags == NULL || (format != RPM_FORMAT_V4 && format != RPM_FORMAT_V6)) {
        return;
    }

    for (i = 0; size_tag_pairs[i].small != 0; i++) {
        normalize_size_pair(tags, size_tag_pairs[i].small, size_tag_pairs[i].large, format);
    }

    if (format == RPM_FORMAT_V6) {
        drop_tags(tags, v4_only_tags, V4_ONLY_NTAGS);
    } else {
        drop_tags(tags, v6_only_tags, V6_ONLY_NTAGS);
    }

    return;
}

/*
 * Build the JSON entry for one main header tag with a placeholder
 * value.  update_payload_tags() puts the real digests and sizes in
 * once the payload exists, so all a placeholder has to do is take up
 * the same room the real value will.  Returns an allocated
 * json_object, NULL for a tag we do not know.
 */
static struct json_object *
make_header_tag(const rpmTagVal tag, const int format)
{
    size_t i = 0;
    char *nul = NULL;
    rpmTagType type = RPM_NULL_TYPE;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (tag == RPMTAG_PAYLOAD_DIGEST_ALGO) {
        /* tarpm writes the digest algorithms out by name */
        type = RPM_STRING_TYPE;
        nul = strdigestalgo(PGPHASHALGO_SHA256);
        value = json_object_new_string(nul);
        free(nul);
    } else if (tag == RPMTAG_RPMFORMAT_VALUE) {
        type = RPM_INT32_TYPE;
        value = json_object_new_int(format);
    } else if (tag == RPMTAG_PAYLOADSIZE_VALUE || tag == RPMTAG_PAYLOADSIZEALT_VALUE) {
        type = RPM_INT64_TYPE;
        value = json_object_new_int64(0);
    }

    for (i = 0; value == NULL && payload_digests[i].tag != 0; i++) {
        if (payload_digests[i].tag != tag) {
            continue;
        }

        /* a digest of all zeroes, which is what rpm sizes its own header with */
        nul = nul_digest(payload_digests[i].type);

        if (nul == NULL) {
            return NULL;
        }

        /* 5092 and 5097 are string arrays, the other four are strings */
        if (tag == RPMTAG_PAYLOADSHA256_VALUE || tag == RPMTAG_PAYLOADSHA256ALT_VALUE) {
            type = RPM_STRING_ARRAY_TYPE;
            value = json_object_new_array();
            json_object_array_add(value, json_object_new_string(nul));
        } else {
            type = RPM_STRING_TYPE;
            value = json_object_new_string(nul);
        }

        free(nul);
    }

    if (value == NULL) {
        warnx(_("*** unknown main header tag: %d"), tag);
        return NULL;
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, value);

    return entry;
}

/*
 * Set the value of a payload tag, which rpm records as a plain string
 * for some tags and as a one element string array for others.
 * Returns non-zero on failure.
 */
static int
set_payload_tag(struct json_object *tags, const rpmTagVal tag, const char *value)
{
    int idx = -1;
    struct json_object *entry = NULL;
    struct json_object *old = NULL;

    idx = find_tag(tags, tag);

    if (idx < 0) {
        return -1;
    }

    entry = json_object_array_get_idx(tags, idx);

    if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &old)) {
        return -1;
    }

    if (json_object_get_type(old) == json_type_array) {
        if (json_object_array_length(old) < 1) {
            return -1;
        }

        json_object_array_put_idx(old, 0, json_object_new_string(value));
    } else {
        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
    }

    return 0;
}

/* Set the value of one of the 64 bit payload size tags.  Returns non-zero on failure. */
static int
set_size_tag(struct json_object *tags, const rpmTagVal tag, const uint64_t size)
{
    int idx = -1;
    struct json_object *entry = NULL;

    idx = find_tag(tags, tag);

    if (idx < 0) {
        return -1;
    }

    entry = json_object_array_get_idx(tags, idx);
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_int64(size));

    return 0;
}

/* Set a string tag, adding it if the header does not carry it yet */
static void
set_string_tag(struct json_object *tags, const rpmTagVal tag, const char *value)
{
    struct json_object *entry = NULL;

    if (set_tag_value(tags, rpmTagGetName(tag), value) == 0) {
        return;
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
    json_object_array_add(tags, entry);

    return;
}

/*
 * Set the payload compressor a v6 package carries.  rpm picks zstd
 * from v6 on, so a package handed to us as gzip has to change over.
 * One that is already zstd keeps the level it came with so we write
 * the same payload back out.
 */
static void
set_v6_compressor(struct json_object *tags)
{
    const char *compressor = NULL;

    compressor = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR));

    if (compressor != NULL && !strcmp(compressor, RPMFORMAT_V6_COMPRESSOR)) {
        return;
    }

    set_string_tag(tags, RPMTAG_PAYLOADCOMPRESSOR, RPMFORMAT_V6_COMPRESSOR);
    set_string_tag(tags, RPMTAG_PAYLOADFLAGS, RPMFORMAT_V6_COMPRESSOR_LEVEL);

    return;
}

/*
 * Put the main header in to the shape the format calls for.  The tags
 * the other format owns come out, the ones this format needs go in
 * with placeholder values, and a v6 package picks up the compressor
 * rpm would have given it.  Returns non-zero on failure.
 */
int
apply_rpmformat(struct json_object *header, const int format)
{
    size_t i = 0;
    size_t ntags = 0;
    const rpmTagVal *group = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;

    if (header == NULL) {
        return -1;
    }

    group = header_tags(format, &ntags);

    if (group == NULL) {
        warnx(_("*** unsupported RPM format: %d"), format);
        return -1;
    }

    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    filter_format_tags(tags, format);

    for (i = 0; i < ntags; i++) {
        if (find_tag(tags, group[i]) >= 0) {
            continue;
        }

        entry = make_header_tag(group[i], format);

        if (entry == NULL) {
            return -1;
        }

        json_object_array_add(tags, entry);
    }

    if (format == RPM_FORMAT_V6) {
        set_v6_compressor(tags);
    }

    return 0;
}

/*
 * Work out the payload digests and sizes and write them in to the
 * main header.  The payload has to exist by the time we are called.
 * Returns non-zero on failure.
 */
int
update_payload_tags(struct json_object *header, const int payloadfd, const int format)
{
    size_t i = 0;
    size_t ntags = 0;
    uint64_t payloadsize = 0;
    uint64_t archivesize = 0;
    char *digest = NULL;
    const rpmTagVal *group = NULL;
    struct json_object *tags = NULL;

    if (header == NULL || payloadfd == -1) {
        return -1;
    }

    group = header_tags(format, &ntags);

    if (group == NULL) {
        warnx(_("*** unsupported RPM format: %d"), format);
        return -1;
    }

    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    for (i = 0; payload_digests[i].tag != 0; i++) {
        if (!in_group(group, ntags, payload_digests[i].tag)) {
            continue;
        }

        if (payload_digests[i].uncompressed) {
            digest = archive_digest(payload_digests[i].type, payloadfd, &archivesize);
        } else {
            digest = payload_digest(payload_digests[i].type, payloadfd, &payloadsize);
        }

        if (digest == NULL) {
            warnx(_("*** failed to compute the %s digest"), rpmTagGetName(payload_digests[i].tag));
            return -1;
        }

        if (set_payload_tag(tags, payload_digests[i].tag, digest) != 0) {
            warnx(_("*** failed to update %s in the header"), rpmTagGetName(payload_digests[i].tag));
            free(digest);
            return -1;
        }

        free(digest);
    }

    /* only a v6 header records the payload sizes */
    if (!in_group(group, ntags, RPMTAG_PAYLOADSIZE_VALUE)) {
        return 0;
    }

    if (set_size_tag(tags, RPMTAG_PAYLOADSIZE_VALUE, payloadsize) != 0 || set_size_tag(tags, RPMTAG_PAYLOADSIZEALT_VALUE, archivesize) != 0) {
        warnx(_("*** failed to update the payload sizes in the header"));
        return -1;
    }

    return 0;
}
