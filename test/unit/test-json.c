/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_json(void)
{
    return 0;
}

int
clean_test_json(void)
{
    return 0;
}

void
test_generate_json(void)
{
    TARPM_ASSERT_TRUE(generate_json(NULL, NULL) == NULL);

    return;
}

void
test_generate_json_entries(void)
{
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, NULL, true) == NULL);
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, NULL, false) == NULL);

    return;
}

void
test_write_json_file(void)
{
    TARPM_ASSERT_TRUE(write_json_file(NULL, NULL, NULL) == -1);

    return;
}

void
test_create_json_entry_null(void)
{
    struct json_object *entry = NULL;

    /* NULL hdrentry returns NULL */
    entry = create_json_entry(NULL, true);
    TARPM_ASSERT_PTR_NULL(entry);

    entry = create_json_entry(NULL, false);
    TARPM_ASSERT_PTR_NULL(entry);

    return;
}

void
test_create_json_entry_non_signature(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    /* non-signature entry should not have read-only field */
    hdrentry.tag = htonl(RPMTAG_NAME);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, false);
    TARPM_ASSERT_PTR_NOT_NULL(entry);

    /* should not have read-only field */
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));

    json_object_put(entry);

    return;
}

void
test_create_json_entry_signature_digest_tags(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    /* MD5 tag should NOT be read-only (recalculated by update_signature) */
    hdrentry.tag = htonl(RPMSIGTAG_MD5);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(16);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    /* SHA1 tag should NOT be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_SHA1);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    /* SHA256 tag should NOT be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_SHA256);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    return;
}

void
test_create_json_entry_signature_size_tags(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    /* SIZE tag should NOT be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_SIZE);
    hdrentry.type = htonl(RPM_INT32_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    /* LONGSIZE tag should NOT be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_LONGSIZE);
    hdrentry.type = htonl(RPM_INT64_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    /* PAYLOADSIZE tag should NOT be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_PAYLOADSIZE);
    hdrentry.type = htonl(RPM_INT32_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
    json_object_put(entry);

    return;
}

void
test_create_json_entry_signature_crypto_tags(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;
    const char *readonly_str = NULL;

    /* RSA tag should be read-only (cryptographic signature) */
    hdrentry.tag = htonl(RPMSIGTAG_RSA);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(512);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    readonly_str = json_object_get_string(value);
    TARPM_ASSERT_STRING_EQUAL(readonly_str, "true");
    json_object_put(entry);

    /* DSA tag should be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_DSA);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(512);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    readonly_str = json_object_get_string(value);
    TARPM_ASSERT_STRING_EQUAL(readonly_str, "true");
    json_object_put(entry);

    /* GPG tag should be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_GPG);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(512);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    readonly_str = json_object_get_string(value);
    TARPM_ASSERT_STRING_EQUAL(readonly_str, "true");
    json_object_put(entry);

    /* PGP tag should be read-only */
    hdrentry.tag = htonl(RPMSIGTAG_PGP);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(512);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    readonly_str = json_object_get_string(value);
    TARPM_ASSERT_STRING_EQUAL(readonly_str, "true");
    json_object_put(entry);

    return;
}

void
test_create_json_entry_has_required_fields(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    /* verify all required fields are present */
    hdrentry.tag = htonl(RPMTAG_NAME);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, false);
    TARPM_ASSERT_PTR_NOT_NULL(entry);

    /* should have "tag" field */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "tag", &value));
    TARPM_ASSERT_PTR_NOT_NULL(value);

    /* should have "type" field */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "type", &value));
    TARPM_ASSERT_PTR_NOT_NULL(value);

    json_object_put(entry);

    return;
}

void
test_read_json_file_missing(void)
{
    /* a NULL input file returns nothing */
    TARPM_ASSERT_PTR_NULL(read_json_file(NULL));

    /* a file that does not exist returns nothing */
    TARPM_ASSERT_PTR_NULL(read_json_file("/nonexistent/tarpm-test-read-json-file.json"));

    return;
}

void
test_read_json_file_invalid(void)
{
    int fd = -1;
    char tmpfile[] = "/tmp/tarpm-test-json-XXXXXX";
    const char *contents = "this is not json\n";

    /* write out a file that does not hold JSON data */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(write(fd, contents, strlen(contents)) == (ssize_t) strlen(contents));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* invalid JSON data returns nothing */
    TARPM_ASSERT_PTR_NULL(read_json_file(tmpfile));

    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    return;
}

void
test_read_json_file_valid(void)
{
    int fd = -1;
    char tmpfile[] = "/tmp/tarpm-test-json-XXXXXX";
    const char *contents = "{ \"name\": \"testpkg\", \"epoch\": 47 }";
    struct json_object *data = NULL;
    struct json_object *value = NULL;

    /* write out a file holding known JSON data */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(write(fd, contents, strlen(contents)) == (ssize_t) strlen(contents));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* the data read back should match what was written */
    data = read_json_file(tmpfile);
    TARPM_ASSERT_PTR_NOT_NULL(data);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(data, "name", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "testpkg");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(data, "epoch", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int(value), 47);

    json_object_put(data);
    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("json", init_test_json, clean_test_json);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_json()", test_generate_json) == NULL ||
        CU_add_test(pSuite, "test generate_json_entries()", test_generate_json_entries) == NULL ||
        CU_add_test(pSuite, "test write_json_file()", test_write_json_file) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with NULL", test_create_json_entry_null) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with non-signature", test_create_json_entry_non_signature) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature digest tags", test_create_json_entry_signature_digest_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature size tags", test_create_json_entry_signature_size_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature crypto tags", test_create_json_entry_signature_crypto_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() has required fields", test_create_json_entry_has_required_fields) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with a missing file", test_read_json_file_missing) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with invalid JSON", test_read_json_file_invalid) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with valid JSON", test_read_json_file_valid) == NULL) {
        return NULL;
    }

    return pSuite;
}
