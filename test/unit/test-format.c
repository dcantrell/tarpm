/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <archive.h>
#include <archive_entry.h>
#include <CUnit/Basic.h>
#include <openssl/sha.h>
#include <rpm/rpmtag.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_format(void)
{
    return 0;
}

int
clean_test_format(void)
{
    return 0;
}

/* Build a minimal header with the tags a package always carries */
static struct json_object *
make_test_header(const char *compressor, const char *level)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;

    header = json_object_new_object();
    tags = json_object_new_array();

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_NAME)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string("test"));
    json_object_array_add(tags, entry);

    if (compressor != NULL) {
        entry = json_object_new_object();
        json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR)));
        json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(compressor));
        json_object_array_add(tags, entry);
    }

    if (level != NULL) {
        entry = json_object_new_object();
        json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_PAYLOADFLAGS)));
        json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
        json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(level));
        json_object_array_add(tags, entry);
    }

    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, tags);

    return header;
}

/* Add one tag holding a single number to a tags array */
static void
add_number_tag(struct json_object *tags, const rpmTagVal tag, const rpmTagType type, const int64_t value)
{
    struct json_object *entry = NULL;

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_int64(value));
    json_object_array_add(tags, entry);

    return;
}

/* Add one tag holding an array of numbers to a tags array */
static void
add_number_array_tag(struct json_object *tags, const rpmTagVal tag, const rpmTagType type, const int64_t *values, const size_t count)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    value = json_object_new_array();

    for (i = 0; i < count; i++) {
        json_object_array_add(value, json_object_new_int64(values[i]));
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, value);
    json_object_array_add(tags, entry);

    return;
}

/* Add one tag holding an array of strings to a tags array */
static void
add_string_array_tag(struct json_object *tags, const rpmTagVal tag, const char **values, const size_t count)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    value = json_object_new_array();

    for (i = 0; i < count; i++) {
        json_object_array_add(value, json_object_new_string(values[i]));
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, value);
    json_object_array_add(tags, entry);

    return;
}

/* Give back the tags array of a test header */
static struct json_object *
header_tags(struct json_object *header)
{
    struct json_object *tags = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        return NULL;
    }

    return tags;
}

/* Find a tag entry by number, NULL if the header does not carry it */
static struct json_object *
lookup_tag(struct json_object *header, const rpmTagVal tag)
{
    size_t i = 0;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        return NULL;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (entry != NULL && get_tag_number(entry, false) == tag) {
            return entry;
        }
    }

    return NULL;
}

/* Give back the string value of a tag entry, NULL if there is none */
static const char *
tag_string(struct json_object *entry)
{
    struct json_object *value = NULL;

    if (entry == NULL || !json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
        return NULL;
    }

    if (json_object_get_type(value) == json_type_array) {
        value = json_object_array_get_idx(value, 0);
    }

    return json_object_get_string(value);
}

/* Give back the type a tag entry records */
static rpmTagType
entry_type(struct json_object *entry)
{
    struct json_object *type = NULL;

    if (entry == NULL || !json_object_object_get_ex(entry, RPM_ENTRY_TYPE_DESC, &type)) {
        return RPM_NULL_TYPE;
    }

    return tag_type(type);
}

/* Give back the first number a tag entry records */
static int64_t
tag_number(struct json_object *entry)
{
    struct json_object *value = NULL;

    if (entry == NULL || !json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
        return -1;
    }

    if (json_object_get_type(value) == json_type_array) {
        value = json_object_array_get_idx(value, 0);
    }

    return json_object_get_int64(value);
}

/* Return true if the group holds the tag */
static bool
tag_in_group(const rpmTagVal *group, const size_t ngroup, const rpmTagVal tag)
{
    size_t i = 0;

    for (i = 0; i < ngroup; i++) {
        if (group[i] == tag) {
            return true;
        }
    }

    return false;
}

/* Write a gzip compressed cpio payload holding one file */
static int
make_test_payload(const char *content)
{
    int fd = -1;
    struct archive *out = NULL;
    struct archive_entry *entry = NULL;

    fd = memfd_create("test-format", MFD_CLOEXEC);

    if (fd == -1) {
        return -1;
    }

    out = archive_write_new();
    archive_write_set_format_cpio_newc(out);
    archive_write_add_filter_gzip(out);
    archive_write_open_fd(out, fd);

    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "./payload");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, strlen(content));
    archive_write_header(out, entry);
    archive_write_data(out, content, strlen(content));
    archive_entry_free(entry);

    archive_write_close(out);
    archive_write_free(out);

    return fd;
}

void
test_apply_rpmformat_v4(void)
{
    struct json_object *header = NULL;

    header = make_test_header("gzip", "9");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V4), 0);

    /* the v4 group goes in */
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOAD_DIGEST_ALGO));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256ALT_VALUE));

    /* the algorithm is written out by name, like extraction does */
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOAD_DIGEST_ALGO)), "sha256");

    /* nothing that only a v6 package carries */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA512_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZE_VALUE));

    /* and the compressor is left as we found it */
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADCOMPRESSOR)), "gzip");
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADFLAGS)), "9");

    json_object_put(header);

    return;
}

void
test_apply_rpmformat_v6(void)
{
    struct json_object *header = NULL;

    header = make_test_header("gzip", "9");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V6), 0);

    /* the whole v6 group goes in */
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256ALT_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA512_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA512ALT_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA3_256_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA3_256ALT_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZE_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZEALT_VALUE));

    /* the format tag names the format */
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE)), "6");

    /* the v4 digest algorithm tag has no place here */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOAD_DIGEST_ALGO));

    /* and gzip gives way to the compressor rpm picks from v6 on */
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADCOMPRESSOR)), RPMFORMAT_V6_COMPRESSOR);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADFLAGS)), RPMFORMAT_V6_COMPRESSOR_LEVEL);

    json_object_put(header);

    return;
}

void
test_apply_rpmformat_keeps_zstd_level(void)
{
    struct json_object *header = NULL;

    /* a package already compressed with zstd keeps the level it came with */
    header = make_test_header("zstd", "10");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V6), 0);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADCOMPRESSOR)), "zstd");
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADFLAGS)), "10");

    json_object_put(header);

    return;
}

void
test_apply_rpmformat_v6_to_v4(void)
{
    struct json_object *header = NULL;

    /* going the other way takes the v6 group back out */
    header = make_test_header("zstd", "19");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V6), 0);
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V4), 0);

    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA512_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA512ALT_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA3_256_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA3_256ALT_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZE_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZEALT_VALUE));

    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOAD_DIGEST_ALGO));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256_VALUE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_PAYLOADSHA256ALT_VALUE));

    json_object_put(header);

    return;
}

void
test_apply_rpmformat_bad_input(void)
{
    struct json_object *header = NULL;

    TARPM_ASSERT_NOT_EQUAL(apply_rpmformat(NULL, RPM_FORMAT_V4), 0);

    header = make_test_header("gzip", "9");
    TARPM_ASSERT_NOT_EQUAL(apply_rpmformat(header, 5), 0);
    json_object_put(header);

    /* a header with no tags array is no good to us */
    header = json_object_new_object();
    TARPM_ASSERT_NOT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V4), 0);
    json_object_put(header);

    return;
}

void
test_format_tag_groups(void)
{
    size_t i = 0;
    static const rpmTagVal common[] = { RPMFORMAT_COMMON_HEADER_TAGS };
    static const rpmTagVal v4only[] = { RPMFORMAT_V4_ONLY_HEADER_TAGS };
    static const rpmTagVal v6only[] = { RPMFORMAT_V6_ONLY_HEADER_TAGS };

    /* a tag belongs to one group and no more */
    for (i = 0; i < (sizeof(common) / sizeof(common[0])); i++) {
        TARPM_ASSERT_FALSE(tag_in_group(v4only, sizeof(v4only) / sizeof(v4only[0]), common[i]));
        TARPM_ASSERT_FALSE(tag_in_group(v6only, sizeof(v6only) / sizeof(v6only[0]), common[i]));
    }

    for (i = 0; i < (sizeof(v4only) / sizeof(v4only[0])); i++) {
        TARPM_ASSERT_FALSE(tag_in_group(v6only, sizeof(v6only) / sizeof(v6only[0]), v4only[i]));
    }

    return;
}

void
test_filter_format_tags_to_v4(void)
{
    static const int64_t sizes[] = { 11, 22, 33 };
    static const char *signatures[] = { "0302", "0302", "0302" };
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /* a header the way a v6 package leaves one */
    header = make_test_header("zstd", "19");
    tags = header_tags(header);
    add_string_array_tag(tags, RPMTAG_FILESIGNATURES, signatures, 3);
    add_number_tag(tags, RPMTAG_RPMFORMAT_VALUE, RPM_INT32_TYPE, 6);
    add_number_tag(tags, RPMTAG_PAYLOADSIZE_VALUE, RPM_INT64_TYPE, 4096);
    add_number_tag(tags, RPMTAG_LONGSIZE, RPM_INT64_TYPE, 66);
    add_number_array_tag(tags, RPMTAG_LONGFILESIZES, RPM_INT64_TYPE, sizes, 3);

    filter_format_tags(tags, RPM_FORMAT_V4);

    /* the tags only a v6 header carries are gone */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOADSIZE_VALUE));

    /* and the sizes moved over to the 32 bit spelling, numbers intact */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_LONGSIZE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_LONGFILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_SIZE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_FILESIZES));
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_SIZE)), 66);
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_FILESIZES)), 11);
    TARPM_ASSERT_EQUAL(entry_type(lookup_tag(header, RPMTAG_SIZE)), RPM_INT32_TYPE);
    TARPM_ASSERT_EQUAL(entry_type(lookup_tag(header, RPMTAG_FILESIZES)), RPM_INT32_TYPE);

    /* a v4 header may hold the file signatures */
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_FILESIGNATURES));

    /* a tag neither group owns stays put */
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_NAME));

    json_object_put(header);

    return;
}

void
test_filter_format_tags_to_v6(void)
{
    static const int64_t sizes[] = { 11, 22, 33 };
    static const int64_t classes[] = { 0, 0, 1 };
    static const char *signatures[] = { "0302", "0302", "0302" };
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /* a header the way a v4 package leaves one */
    header = make_test_header("gzip", "9");
    tags = header_tags(header);
    add_number_tag(tags, RPMTAG_PAYLOAD_DIGEST_ALGO, RPM_INT32_TYPE, 8);
    add_number_tag(tags, RPMTAG_SIZE, RPM_INT32_TYPE, 66);
    add_number_array_tag(tags, RPMTAG_FILESIZES, RPM_INT32_TYPE, sizes, 3);
    add_number_array_tag(tags, RPMTAG_FILECLASS, RPM_INT32_TYPE, classes, 3);
    add_string_array_tag(tags, RPMTAG_FILESIGNATURES, signatures, 3);

    filter_format_tags(tags, RPM_FORMAT_V6);

    /* the tags only a v4 header carries are gone */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_PAYLOAD_DIGEST_ALGO));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_FILECLASS));

    /* including the file signatures only a v4 header may hold */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_FILESIGNATURES));

    /* and the sizes moved over to the 64 bit spelling, numbers intact */
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_SIZE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_LONGSIZE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_LONGFILESIZES));
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_LONGSIZE)), 66);
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_LONGFILESIZES)), 11);
    TARPM_ASSERT_EQUAL(entry_type(lookup_tag(header, RPMTAG_LONGSIZE)), RPM_INT64_TYPE);
    TARPM_ASSERT_EQUAL(entry_type(lookup_tag(header, RPMTAG_LONGFILESIZES)), RPM_INT64_TYPE);

    json_object_put(header);

    return;
}

void
test_filter_format_tags_keeps_large_sizes(void)
{
    static const int64_t sizes[] = { 11, 5000000000LL };
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /* a v4 package counts in 64 bits when a number will not fit */
    header = make_test_header("gzip", "9");
    tags = header_tags(header);
    add_number_tag(tags, RPMTAG_LONGSIZE, RPM_INT64_TYPE, 5000000000LL);
    add_number_array_tag(tags, RPMTAG_LONGFILESIZES, RPM_INT64_TYPE, sizes, 2);

    filter_format_tags(tags, RPM_FORMAT_V4);

    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_SIZE));
    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_LONGSIZE));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_LONGFILESIZES));

    json_object_put(header);

    return;
}

void
test_filter_format_tags_prefers_32_bit_sizes(void)
{
    static const int64_t fresh[] = { 11, 22 };
    static const int64_t stale[] = { 99, 99 };
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /*
     * A header carrying both spellings keeps the 32 bit one, since
     * that is the one we rebuild from the payload tree.
     */
    header = make_test_header("gzip", "9");
    tags = header_tags(header);
    add_number_array_tag(tags, RPMTAG_LONGFILESIZES, RPM_INT64_TYPE, stale, 2);
    add_number_array_tag(tags, RPMTAG_FILESIZES, RPM_INT32_TYPE, fresh, 2);

    filter_format_tags(tags, RPM_FORMAT_V6);

    TARPM_ASSERT_PTR_NULL(lookup_tag(header, RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_LONGFILESIZES));
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_LONGFILESIZES)), 11);

    json_object_put(header);

    return;
}

void
test_filter_format_tags_twice(void)
{
    static const int64_t sizes[] = { 11, 22 };
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /* running over the same array again changes nothing */
    header = make_test_header("gzip", "9");
    tags = header_tags(header);
    add_number_tag(tags, RPMTAG_SIZE, RPM_INT32_TYPE, 66);
    add_number_array_tag(tags, RPMTAG_FILESIZES, RPM_INT32_TYPE, sizes, 2);

    filter_format_tags(tags, RPM_FORMAT_V6);
    filter_format_tags(tags, RPM_FORMAT_V6);

    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 5);
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_LONGSIZE)), 66);
    TARPM_ASSERT_EQUAL(tag_number(lookup_tag(header, RPMTAG_LONGFILESIZES)), 11);

    json_object_put(header);

    return;
}

void
test_filter_format_tags_bad_input(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    /* nothing to do and nothing thrown away */
    filter_format_tags(NULL, RPM_FORMAT_V4);

    header = make_test_header("gzip", "9");
    tags = header_tags(header);
    add_number_tag(tags, RPMTAG_RPMFORMAT_VALUE, RPM_INT32_TYPE, 6);

    filter_format_tags(tags, 5);
    TARPM_ASSERT_PTR_NOT_NULL(lookup_tag(header, RPMTAG_RPMFORMAT_VALUE));

    json_object_put(header);

    return;
}

void
test_update_payload_tags_v4(void)
{
    int fd = -1;
    char *digest = NULL;
    struct json_object *header = NULL;

    fd = make_test_payload("v4 payload data");
    TARPM_ASSERT_TRUE(fd != -1);

    header = make_test_header("gzip", "9");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V4), 0);
    TARPM_ASSERT_EQUAL(update_payload_tags(header, fd, RPM_FORMAT_V4), 0);

    /* the digest of the payload as it lands in the package */
    digest = payload_digest(TARPM_DIGEST_SHA256, fd, NULL);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADSHA256_VALUE)), digest);
    free(digest);

    /* and the digest of the same payload uncompressed */
    digest = archive_digest(TARPM_DIGEST_SHA256, fd, NULL);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADSHA256ALT_VALUE)), digest);
    free(digest);

    json_object_put(header);
    close(fd);

    return;
}

void
test_update_payload_tags_v6(void)
{
    int fd = -1;
    uint64_t payloadsize = 0;
    uint64_t archivesize = 0;
    char *digest = NULL;
    struct json_object *header = NULL;
    struct json_object *value = NULL;

    fd = make_test_payload("v6 payload data");
    TARPM_ASSERT_TRUE(fd != -1);

    header = make_test_header("zstd", "19");
    TARPM_ASSERT_EQUAL(apply_rpmformat(header, RPM_FORMAT_V6), 0);
    TARPM_ASSERT_EQUAL(update_payload_tags(header, fd, RPM_FORMAT_V6), 0);

    /* every digest in the v6 group describes the payload we have */
    digest = payload_digest(TARPM_DIGEST_SHA512, fd, &payloadsize);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADSHA512_VALUE)), digest);
    free(digest);

    digest = archive_digest(TARPM_DIGEST_SHA3_256, fd, &archivesize);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_STRING_EQUAL(tag_string(lookup_tag(header, RPMTAG_PAYLOADSHA3_256ALT_VALUE)), digest);
    free(digest);

    /* the sizes come from the same two passes over the payload */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(lookup_tag(header, RPMTAG_PAYLOADSIZE_VALUE), RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_TRUE(json_object_get_int64(value) == (int64_t) payloadsize);

    TARPM_ASSERT_TRUE(json_object_object_get_ex(lookup_tag(header, RPMTAG_PAYLOADSIZEALT_VALUE), RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_TRUE(json_object_get_int64(value) == (int64_t) archivesize);

    json_object_put(header);
    close(fd);

    return;
}

void
test_update_payload_tags_bad_input(void)
{
    int fd = -1;
    struct json_object *header = NULL;

    fd = make_test_payload("payload data");
    TARPM_ASSERT_TRUE(fd != -1);

    TARPM_ASSERT_NOT_EQUAL(update_payload_tags(NULL, fd, RPM_FORMAT_V4), 0);

    header = make_test_header("gzip", "9");
    TARPM_ASSERT_NOT_EQUAL(update_payload_tags(header, -1, RPM_FORMAT_V4), 0);
    TARPM_ASSERT_NOT_EQUAL(update_payload_tags(header, fd, 5), 0);

    /* the format tags have to be there before we can fill them in */
    TARPM_ASSERT_NOT_EQUAL(update_payload_tags(header, fd, RPM_FORMAT_V4), 0);

    json_object_put(header);
    close(fd);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("format", init_test_format, clean_test_format);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test apply_rpmformat() v4", test_apply_rpmformat_v4) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() v6", test_apply_rpmformat_v6) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() keeps the zstd level", test_apply_rpmformat_keeps_zstd_level) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() v6 back to v4", test_apply_rpmformat_v6_to_v4) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() bad input", test_apply_rpmformat_bad_input) == NULL ||
        CU_add_test(pSuite, "test the format tag groups do not overlap", test_format_tag_groups) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() down to v4", test_filter_format_tags_to_v4) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() up to v6", test_filter_format_tags_to_v6) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() keeps large sizes", test_filter_format_tags_keeps_large_sizes) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() prefers the 32 bit sizes", test_filter_format_tags_prefers_32_bit_sizes) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() run twice", test_filter_format_tags_twice) == NULL ||
        CU_add_test(pSuite, "test filter_format_tags() bad input", test_filter_format_tags_bad_input) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() v4", test_update_payload_tags_v4) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() v6", test_update_payload_tags_v6) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() bad input", test_update_payload_tags_bad_input) == NULL) {
        return NULL;
    }

    return pSuite;
}
