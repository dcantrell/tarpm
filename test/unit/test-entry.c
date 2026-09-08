/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmbase64.h>
#include <json.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_entry(void)
{
    return 0;
}

int
clean_test_entry(void)
{
    return 0;
}

void
test_is_file_tag(void)
{
    /* RPMTAG_SPEC should be a file tag */
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_DESCRIPTION));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREIN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTIN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREUN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTUN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PRETRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTTRANS));
#ifdef _HAS_UNTRANS_TAG
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREUNTRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTUNTRANS));
#endif
#ifdef _HAS_SPEC_TAG
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_SPEC));
#endif

    /* other common tags should not be file tags */
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_NAME));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_VERSION));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_RELEASE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_SUMMARY));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_BUILDTIME));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_BUILDHOST));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_SIZE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_LICENSE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_PACKAGER));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_GROUP));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_URL));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_OS));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_ARCH));

    return;
}

void
test_get_tag_filename(void)
{
    char *filename = NULL;

#ifdef _HAS_SPEC_TAG
    /* test RPMTAG_SPEC with default ending */
    filename = get_tag_filename(RPMTAG_SPEC, NULL);
    TARPM_ASSERT_TRUE(filename != NULL);
    TARPM_ASSERT_TRUE(strcmp(filename, "spec") == 0);
    free(filename);

    /* test RPMTAG_SPEC with .txt ending */
    filename = get_tag_filename(RPMTAG_SPEC, "txt");
    TARPM_ASSERT_TRUE(filename != NULL);
    TARPM_ASSERT_TRUE(strcmp(filename, "spec.txt") == 0);
    free(filename);
#endif

    /* test non-file tag should return NULL */
    filename = get_tag_filename(RPMTAG_NAME, NULL);
    TARPM_ASSERT_TRUE(filename == NULL);

    filename = get_tag_filename(RPMTAG_VERSION, "txt");
    TARPM_ASSERT_TRUE(filename == NULL);

    return;
}

void
test_add_entry_value_null(void)
{
    struct json_object *arrayentry = NULL;
    uint8_t buffer[4];

    memset(buffer, 0, sizeof(buffer));

    /* a NULL array entry should not crash */
    add_entry_value(NULL, RPMTAG_NAME, buffer, 0, RPM_STRING_TYPE, 1, NULL);

    /* a NULL buffer should not add anything */
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, NULL, 0, RPM_STRING_TYPE, 1, NULL);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, NULL));
    json_object_put(arrayentry);

    return;
}

void
test_add_entry_value_scalars(void)
{
    struct json_object *arrayentry = NULL;
    struct json_object *value = NULL;
    uint8_t buffer[8];
    uint16_t val16 = 0;
    uint32_t val32 = 0;
    uint64_t val64 = 0;

    memset(buffer, 0, sizeof(buffer));

    /* a null type carries no data */
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_NULL_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "(null)");
    json_object_put(arrayentry);

    /* a single character */
    buffer[0] = 'x';
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_CHAR_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "x");
    json_object_put(arrayentry);

    /* an eight bit integer */
    buffer[0] = 47;
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_INT8_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_get_int(value), 47);
    json_object_put(arrayentry);

    /* a sixteen bit integer */
    val16 = htons(4700);
    memcpy(buffer, &val16, sizeof(val16));
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_INT16_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_get_int(value), 4700);
    json_object_put(arrayentry);

    /* a thirty two bit integer */
    val32 = htonl(470000);
    memcpy(buffer, &val32, sizeof(val32));
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_INT32_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_get_int(value), 470000);
    json_object_put(arrayentry);

    /* a sixty four bit integer */
    val64 = htobe64(47000000000ULL);
    memcpy(buffer, &val64, sizeof(val64));
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_INT64_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_get_int64(value), 47000000000LL);
    json_object_put(arrayentry);

    return;
}

void
test_add_entry_value_int_arrays(void)
{
    struct json_object *arrayentry = NULL;
    struct json_object *value = NULL;
    uint8_t buffer[12];
    uint32_t val32 = 0;

    memset(buffer, 0, sizeof(buffer));

    /* three thirty two bit integers */
    val32 = htonl(1);
    memcpy(buffer, &val32, sizeof(val32));
    val32 = htonl(2);
    memcpy(buffer + sizeof(val32), &val32, sizeof(val32));
    val32 = htonl(3);
    memcpy(buffer + (2 * sizeof(val32)), &val32, sizeof(val32));

    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_FILESIZES, buffer, 0, RPM_INT32_TYPE, 3, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_array_length(value), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(value, 0)), 1);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(value, 1)), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(value, 2)), 3);
    json_object_put(arrayentry);

    return;
}

void
test_add_entry_value_strings(void)
{
    struct json_object *arrayentry = NULL;
    struct json_object *value = NULL;
    uint8_t buffer[16];

    memset(buffer, 0, sizeof(buffer));

    /* a single string for a tag that is not written out to a file */
    memcpy(buffer, "testpkg", 8);
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, RPM_STRING_TYPE, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "testpkg");
    json_object_put(arrayentry);

    /* an array of strings */
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "foo\0bar\0baz", 12);
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_BASENAMES, buffer, 0, RPM_STRING_ARRAY_TYPE, 3, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_EQUAL(json_object_array_length(value), 3);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(value, 0)), "foo");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(value, 1)), "bar");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(value, 2)), "baz");
    json_object_put(arrayentry);

    return;
}

void
test_add_entry_value_binary(void)
{
    struct json_object *arrayentry = NULL;
    struct json_object *value = NULL;
    uint8_t buffer[4];
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    int r = 0;

    /* a binary blob is stored as a base64 string */
    buffer[0] = 0x00;
    buffer[1] = 0x01;
    buffer[2] = 0x02;
    buffer[3] = 0x03;

    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_SIGMD5, buffer, 0, RPM_BIN_TYPE, sizeof(buffer), NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));

    /* the encoded value should decode back to the original bytes */
    r = rpmBase64Decode(json_object_get_string(value), (void **) &blob, &blobsize);
    TARPM_ASSERT_EQUAL(r, 0);
    TARPM_ASSERT_EQUAL(blobsize, sizeof(buffer));
    TARPM_ASSERT_TRUE(memcmp(blob, buffer, sizeof(buffer)) == 0);

    free(blob);
    json_object_put(arrayentry);

    return;
}

void
test_add_entry_value_unknown(void)
{
    struct json_object *arrayentry = NULL;
    struct json_object *value = NULL;
    uint8_t buffer[4];

    memset(buffer, 0, sizeof(buffer));

    /* an unrecognized data type is noted as unknown */
    arrayentry = json_object_new_object();
    add_entry_value(arrayentry, RPMTAG_NAME, buffer, 0, 47, 1, NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(arrayentry, RPM_ENTRY_VALUE_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "(unknown)");
    json_object_put(arrayentry);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("entry", init_test_entry, clean_test_entry);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test is_file_tag()", test_is_file_tag) == NULL ||
        CU_add_test(pSuite, "test get_tag_filename()", test_get_tag_filename) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with NULL", test_add_entry_value_null) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with scalars", test_add_entry_value_scalars) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with integer arrays", test_add_entry_value_int_arrays) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with strings", test_add_entry_value_strings) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with a binary blob", test_add_entry_value_binary) == NULL ||
        CU_add_test(pSuite, "test add_entry_value() with an unknown type", test_add_entry_value_unknown) == NULL) {
        return NULL;
    }

    return pSuite;
}
