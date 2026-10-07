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

    /* and gzip gives way to the compressor rpm picks from format 6 on */
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
    if (CU_add_test(pSuite, "test apply_rpmformat() format 4", test_apply_rpmformat_v4) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() format 6", test_apply_rpmformat_v6) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() keeps the zstd level", test_apply_rpmformat_keeps_zstd_level) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() format 6 back to 4", test_apply_rpmformat_v6_to_v4) == NULL ||
        CU_add_test(pSuite, "test apply_rpmformat() bad input", test_apply_rpmformat_bad_input) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() format 4", test_update_payload_tags_v4) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() format 6", test_update_payload_tags_v6) == NULL ||
        CU_add_test(pSuite, "test update_payload_tags() bad input", test_update_payload_tags_bad_input) == NULL) {
        return NULL;
    }

    return pSuite;
}
