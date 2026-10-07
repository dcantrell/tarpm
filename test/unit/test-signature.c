/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <CUnit/Basic.h>
#include <json.h>
#include <rpm/rpmbase64.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_signature(void)
{
    return 0;
}

int
clean_test_signature(void)
{
    return 0;
}

/* Return the entry for a tag name, NULL if the signature lacks it */
static struct json_object *
find_tag(struct json_object *signature, const char *tag)
{
    size_t i = 0;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (signature == NULL || json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        return NULL;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &value) == 1 && !strcmp(tag, json_object_get_string(value))) {
            return entry;
        }
    }

    return NULL;
}

/* Return how many bytes the value of a binary blob entry holds */
static size_t
blob_size(struct json_object *entry)
{
    struct json_object *value = NULL;
    uint8_t *blob = NULL;
    size_t size = 0;

    if (entry == NULL || json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value) == 0) {
        return 0;
    }

    if (rpmBase64Decode(json_object_get_string(value), (void **) &blob, &size) != 0) {
        return 0;
    }

    free(blob);

    return size;
}

/* Return the number of entries in a signature */
static size_t
tag_count(struct json_object *signature)
{
    struct json_object *tags = NULL;

    if (signature == NULL || json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        return 0;
    }

    return json_object_array_length(tags);
}

/* Assert every entry carries the read-only marker */
static void
assert_read_only(struct json_object *signature)
{
    size_t i = 0;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    if (json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        return;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);
        value = NULL;

        TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_METADATA_READ_ONLY, &value) == 1);
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    }

    return;
}

void
test_read_signature(void)
{
    TARPM_ASSERT_TRUE(read_signature(-47) == NULL);

    return;
}

void
test_make_signature_bad_format(void)
{
    TARPM_ASSERT_PTR_NULL(make_signature(0));
    TARPM_ASSERT_PTR_NULL(make_signature(5));
    TARPM_ASSERT_PTR_NULL(make_signature(7));

    return;
}

void
test_make_signature_v4(void)
{
    size_t i = 0;
    struct json_object *signature = NULL;
    struct json_object *value = NULL;
    const char *present[] = { "Headersignatures", "Sha1", "Sha256", "Size", "Md5", "Payloadsize", "Reservedspace" };
    const char *absent[] = { "Sha3_256", "Reserved", "Rsa", "Dsa", "Longsize" };

    signature = make_signature(RPM_FORMAT_V4);
    TARPM_ASSERT_PTR_NOT_NULL(signature);

    /* the header starts the way every RPM header does */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(signature, RPM_SIGNATURE_MAGIC_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "0x8EADE801");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(signature, RPM_SIGNATURE_RESERVED_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "0000");

    /* the v4 tags are there and nothing else is */
    TARPM_ASSERT_EQUAL(tag_count(signature), sizeof(present) / sizeof(present[0]));

    for (i = 0; i < (sizeof(present) / sizeof(present[0])); i++) {
        TARPM_ASSERT_PTR_NOT_NULL(find_tag(signature, present[i]));
    }

    for (i = 0; i < (sizeof(absent) / sizeof(absent[0])); i++) {
        TARPM_ASSERT_PTR_NULL(find_tag(signature, absent[i]));
    }

    /* the signature header is provided for informational purposes only */
    assert_read_only(signature);

    /* the region tag carries a trailer and the trailer is one index entry */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Headersignatures"), RPM_ENTRY_TRAILER_DESC, &value) == 1);
    TARPM_ASSERT_EQUAL(blob_size(find_tag(signature, "Headersignatures")), sizeof(struct rpmhdrentry));

    /* the blobs are the sizes rpm uses */
    TARPM_ASSERT_EQUAL(blob_size(find_tag(signature, "Md5")), 16);
    TARPM_ASSERT_EQUAL(blob_size(find_tag(signature, "Reservedspace")), RPM_SIGNATURE_RESERVED_SIZE);

    /* the sizes are int32 and the digests are strings */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Size"), RPM_ENTRY_TYPE_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "int32");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Payloadsize"), RPM_ENTRY_TYPE_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "int32");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Sha1"), RPM_ENTRY_TYPE_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "string");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Sha256"), RPM_ENTRY_TYPE_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "string");

    json_object_put(signature);

    return;
}

void
test_make_signature_v6(void)
{
    size_t i = 0;
    struct json_object *signature = NULL;
    struct json_object *value = NULL;
    const char *present[] = { "Headersignatures", "Sha256", "Sha3_256", "Reserved" };
    const char *absent[] = { "Sha1", "Md5", "Size", "Payloadsize", "Reservedspace" };

    signature = make_signature(RPM_FORMAT_V6);
    TARPM_ASSERT_PTR_NOT_NULL(signature);

    /* the v6 header carries digests and the reserved space, nothing else */
    TARPM_ASSERT_EQUAL(tag_count(signature), sizeof(present) / sizeof(present[0]));

    for (i = 0; i < (sizeof(present) / sizeof(present[0])); i++) {
        TARPM_ASSERT_PTR_NOT_NULL(find_tag(signature, present[i]));
    }

    for (i = 0; i < (sizeof(absent) / sizeof(absent[0])); i++) {
        TARPM_ASSERT_PTR_NULL(find_tag(signature, absent[i]));
    }

    assert_read_only(signature);

    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Headersignatures"), RPM_ENTRY_TRAILER_DESC, &value) == 1);
    TARPM_ASSERT_EQUAL(blob_size(find_tag(signature, "Headersignatures")), sizeof(struct rpmhdrentry));
    TARPM_ASSERT_EQUAL(blob_size(find_tag(signature, "Reserved")), RPM_SIGNATURE_RESERVED_SIZE);

    TARPM_ASSERT_TRUE(json_object_object_get_ex(find_tag(signature, "Sha3_256"), RPM_ENTRY_TYPE_DESC, &value) == 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "string");

    json_object_put(signature);

    return;
}

void
test_make_signature_default(void)
{
    struct json_object *signature = NULL;

    /* the default format is the one we hand to make_signature() */
    TARPM_ASSERT_EQUAL(rpmformat, RPM_FORMAT_DEFAULT);
    TARPM_ASSERT_EQUAL(RPM_FORMAT_DEFAULT, RPM_FORMAT_V4);

    signature = make_signature(rpmformat);
    TARPM_ASSERT_PTR_NOT_NULL(signature);
    TARPM_ASSERT_PTR_NOT_NULL(find_tag(signature, "Md5"));

    json_object_put(signature);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("signature", init_test_signature, clean_test_signature);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test read_signature()", test_read_signature) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test make_signature() with a bad format", test_make_signature_bad_format) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test make_signature() for format 4", test_make_signature_v4) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test make_signature() for format 6", test_make_signature_v6) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test make_signature() default format", test_make_signature_default) == NULL) {
        return NULL;
    }

    return pSuite;
}
