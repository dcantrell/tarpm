/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_header(void)
{
    return 0;
}

int
clean_test_header(void)
{
    return 0;
}

void
test_read_header(void)
{
    /* check that invalid input returns an error */
    TARPM_ASSERT_TRUE(read_header(-47, NULL) == NULL);

    return;
}

void
test_valid_header(void)
{
    struct rpmhdr hdr;

    /* NULL input returns false */
    TARPM_ASSERT_FALSE(valid_header(NULL));

    /* invalid magic returns false */
    hdr.magic = 0x12345678;
    hdr.reserved = RPM_SIGNATURE_RESERVED;
    TARPM_ASSERT_FALSE(valid_header(&hdr));

    /* invalid reserved returns false */
    hdr.magic = RPM_SIGNATURE_MAGIC;
    hdr.reserved = 0x12345678;
    TARPM_ASSERT_FALSE(valid_header(&hdr));

    /* valid magic and reserved returns true */
    hdr.magic = RPM_SIGNATURE_MAGIC;
    hdr.reserved = RPM_SIGNATURE_RESERVED;
    TARPM_ASSERT_TRUE(valid_header(&hdr));

    return;
}

void
test_has_trailer(void)
{
    struct rpmhdrentry entries[3];

    /* NULL entries or zero count returns false */
    TARPM_ASSERT_FALSE(has_trailer(0, NULL));
    TARPM_ASSERT_FALSE(has_trailer(0, entries));
    TARPM_ASSERT_FALSE(has_trailer(3, NULL));

    /* no trailer tag returns false */
    entries[0].tag = htonl(RPMSIGTAG_SIZE);
    entries[1].tag = htonl(RPMSIGTAG_MD5);
    entries[2].tag = htonl(RPMSIGTAG_SHA1);
    TARPM_ASSERT_FALSE(has_trailer(3, entries));

    /* HEADER_SIGNATURES tag returns true */
    entries[0].tag = htonl(HEADER_SIGNATURES);
    entries[1].tag = htonl(RPMSIGTAG_MD5);
    entries[2].tag = htonl(RPMSIGTAG_SHA1);
    TARPM_ASSERT_TRUE(has_trailer(3, entries));

    /* HEADER_IMMUTABLE tag returns true */
    entries[0].tag = htonl(RPMSIGTAG_SIZE);
    entries[1].tag = htonl(HEADER_IMMUTABLE);
    entries[2].tag = htonl(RPMSIGTAG_SHA1);
    TARPM_ASSERT_TRUE(has_trailer(3, entries));

    return;
}

void
test_get_trailer_data_null(void)
{
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;

    /* NULL data returns error */
    r = get_trailer_data(NULL, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);

    /* NULL trailer_data returns error */
    r = get_trailer_data(NULL, NULL, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);

    /* NULL trailer_size returns error */
    r = get_trailer_data(NULL, &trailer_data, NULL);
    TARPM_ASSERT_EQUAL(r, -1);

    /* all NULL returns error */
    r = get_trailer_data(NULL, NULL, NULL);
    TARPM_ASSERT_EQUAL(r, -1);

    return;
}

void
test_get_trailer_data_no_tags(void)
{
    struct json_object *data = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;

    /* data without tags array returns error */
    data = json_object_new_object();
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_get_trailer_data_empty_tags(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;

    /* data with empty tags array returns error */
    data = json_object_new_object();
    tags = json_object_new_array();
    json_object_object_add(data, "tags", tags);
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_get_trailer_data_no_trailer(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;

    /* data with tags but no trailer returns error */
    data = json_object_new_object();
    tags = json_object_new_array();
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "type", json_object_new_string("STRING"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);
    json_object_object_add(data, "tags", tags);
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_get_trailer_data_no_value(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *trailer = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;

    /* tag with trailer but no value returns error */
    data = json_object_new_object();
    tags = json_object_new_array();
    entry = json_object_new_object();
    trailer = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Headersignatures"));
    json_object_object_add(entry, "type", json_object_new_string("BIN"));
    json_object_object_add(entry, "trailer", trailer);
    json_object_array_add(tags, entry);
    json_object_object_add(data, "tags", tags);
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_get_trailer_data_invalid_size(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *trailer = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;
    const char *base64_8bytes = NULL;

    /* trailer with size != 16 returns error */
    /* 8 bytes of zeros in base64 */
    base64_8bytes = "AAAAAAAAAAA=";
    data = json_object_new_object();
    tags = json_object_new_array();
    entry = json_object_new_object();
    trailer = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Headersignatures"));
    json_object_object_add(entry, "type", json_object_new_string("BIN"));
    json_object_object_add(entry, "trailer", trailer);
    json_object_object_add(entry, "value", json_object_new_string(base64_8bytes));
    json_object_array_add(tags, entry);
    json_object_object_add(data, "tags", tags);
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_get_trailer_data_valid(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *trailer = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;
    const char *base64_16bytes = NULL;

    /* valid trailer data with size == 16 returns success */
    /* 16 bytes of zeros in base64 */
    base64_16bytes = "AAAAAAAAAAAAAAAAAAAAAA==";
    data = json_object_new_object();
    tags = json_object_new_array();
    entry = json_object_new_object();
    trailer = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Headersignatures"));
    json_object_object_add(entry, "type", json_object_new_string("BIN"));
    json_object_object_add(entry, "trailer", trailer);
    json_object_object_add(entry, "value", json_object_new_string(base64_16bytes));
    json_object_array_add(tags, entry);
    json_object_object_add(data, "tags", tags);
    r = get_trailer_data(data, &trailer_data, &trailer_size);
    TARPM_ASSERT_EQUAL(r, 0);
    TARPM_ASSERT_PTR_NOT_NULL(trailer_data);
    TARPM_ASSERT_EQUAL(trailer_size, 16);
    free(trailer_data);
    json_object_put(data);

    return;
}

void
test_create_header_invalid(void)
{
    struct json_object *data = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    int r = 0;

    /* NULL data returns an error */
    r = create_header(NULL, &hdr, &hdrinfo, NULL, NULL, false);
    TARPM_ASSERT_EQUAL(r, -1);

    /* data with no tags array returns an error */
    data = json_object_new_object();
    r = create_header(data, &hdr, &hdrinfo, NULL, NULL, false);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    /* tags that are not an array returns an error */
    data = json_object_new_object();
    json_object_object_add(data, "tags", json_object_new_string("not an array"));
    r = create_header(data, &hdr, &hdrinfo, NULL, NULL, false);
    TARPM_ASSERT_EQUAL(r, -1);
    json_object_put(data);

    return;
}

void
test_create_header_valid(void)
{
    struct json_object *data = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;
    int r = 0;

    /* a header with two simple tags */
    data = json_object_new_object();
    tags = json_object_new_array();

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "type", json_object_new_string("string"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Version"));
    json_object_object_add(entry, "type", json_object_new_string("string"));
    json_object_object_add(entry, "value", json_object_new_string("1.0"));
    json_object_array_add(tags, entry);

    json_object_object_add(data, "tags", tags);

    r = create_header(data, &hdr, &hdrinfo, NULL, NULL, false);
    TARPM_ASSERT_EQUAL(r, 0);
    TARPM_ASSERT_PTR_NOT_NULL(hdr);
    TARPM_ASSERT_PTR_NOT_NULL(hdrinfo);

    /* the generated header should be valid and hold both tags */
    TARPM_ASSERT_EQUAL(ntohl(hdr->magic), RPM_SIGNATURE_MAGIC);
    TARPM_ASSERT_EQUAL(ntohl(hdr->reserved), RPM_SIGNATURE_RESERVED);
    TARPM_ASSERT_EQUAL(ntohl(hdr->nentries), 2);

    /* the data area holds both strings and their trailing NUL bytes */
    TARPM_ASSERT_EQUAL(ntohl(hdr->nbytes), strlen("testpkg") + strlen("1.0") + 2);

    free(hdrinfo->estart);
    free(hdrinfo->datastart);
    free(hdrinfo);
    free(hdr);
    json_object_put(data);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("header", init_test_header, clean_test_header);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test read_header()", test_read_header) == NULL ||
        CU_add_test(pSuite, "test valid_header()", test_valid_header) == NULL ||
        CU_add_test(pSuite, "test has_trailer()", test_has_trailer) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with NULL", test_get_trailer_data_null) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with no tags", test_get_trailer_data_no_tags) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with empty tags", test_get_trailer_data_empty_tags) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with no trailer", test_get_trailer_data_no_trailer) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with no value", test_get_trailer_data_no_value) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with invalid size", test_get_trailer_data_invalid_size) == NULL ||
        CU_add_test(pSuite, "test get_trailer_data() with valid data", test_get_trailer_data_valid) == NULL ||
        CU_add_test(pSuite, "test create_header() with invalid data", test_create_header_invalid) == NULL ||
        CU_add_test(pSuite, "test create_header() with valid data", test_create_header_valid) == NULL) {
        return NULL;
    }

    return pSuite;
}
