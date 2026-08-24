/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <rpm/rpmtag.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_tags(void)
{
    return 0;
}

int
clean_test_tags(void)
{
    return 0;
}

void
test_strtagtype(void)
{
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_NULL_TYPE), "(null)") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_CHAR_TYPE), "char") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT8_TYPE), "int8") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT16_TYPE), "int16") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT32_TYPE), "int32") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT64_TYPE), "int64") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_STRING_TYPE), "string") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_BIN_TYPE), "binary blob") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_STRING_ARRAY_TYPE), "string array") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_I18NSTRING_TYPE), "i18n string") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(47), "(unknown)") == 0);

    return;
}

void
test_sig_tag_name(void)
{
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(HEADER_SIGNATURES), "Headersignatures") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(HEADER_IMMUTABLE), "Headerimmutable") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SIZE), "Size") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LEMD5_1), "Lemd5_1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PGP), "Pgp") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LEMD5_2), "Lemd5_2") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_MD5), "Md5") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_GPG), "Gpg") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PGP5), "Pgp5") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PAYLOADSIZE), "Payloadsize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_RESERVEDSPACE), "Reservedspace") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_BADSHA1_1), "Badsha1_1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_BADSHA1_2), "Badsha1_2") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_DSA), "Dsa") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_RSA), "Rsa") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SHA1), "Sha1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LONGSIZE), "Longsize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LONGARCHIVESIZE), "Longarchivesize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SHA256), "Sha256") == 0);
#ifdef RPMSIGTAG_FILESIGNATURES
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURES), "Filesignatures") == 0);
#endif
#ifdef RPMSIGTAG_FILESIGNATURELENGTH
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURELENGTH), "Filesignaturelength") == 0);
#endif
#ifdef RPMSIGTAG_VERITYSIGNATURES
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATURES), "Veritysignatures") == 0);
#endif
#ifdef RPMSIGTAG_VERITYSIGNATUREALGO
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATUREALGO), "Veritysignaturealgo") == 0);
#endif
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(0), "(unknown)") == 0);

    return;
}

void
test_tag_type(void)
{
    struct json_object *tag = NULL;

    /* NULL input returns RPM_NULL_TYPE */
    TARPM_ASSERT_TRUE(tag_type(NULL) == RPM_NULL_TYPE);

    /* test all valid type strings */
    tag = json_object_new_string("(null)");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_NULL_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("char");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_CHAR_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("int8");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_INT8_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("int16");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_INT16_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("int32");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_INT32_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("int64");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_INT64_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("string");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_STRING_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("binary blob");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_BIN_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("string array");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_STRING_ARRAY_TYPE);
    json_object_put(tag);

    tag = json_object_new_string("i18n string");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_I18NSTRING_TYPE);
    json_object_put(tag);

    /* unknown type string returns RPM_NULL_TYPE */
    tag = json_object_new_string("unknown");
    TARPM_ASSERT_TRUE(tag_type(tag) == RPM_NULL_TYPE);
    json_object_put(tag);

    return;
}

void
test_get_tag_number(void)
{
    struct json_object *entry = NULL;
    rpmTagVal result = 0;

    /* NULL input returns RPMTAG_NOT_FOUND */
    result = get_tag_number(NULL, false);
    TARPM_ASSERT_TRUE(result == RPMTAG_NOT_FOUND);

    /* test with valid RPM tag name */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    result = get_tag_number(entry, false);
    TARPM_ASSERT_TRUE(result == RPMTAG_NAME);
    json_object_put(entry);

    /* test with valid signature tag name */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Sha256"));
    result = get_tag_number(entry, true);
    TARPM_ASSERT_TRUE(result == RPMSIGTAG_SHA256);
    json_object_put(entry);

    /* test with another RPM tag */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Version"));
    result = get_tag_number(entry, false);
    TARPM_ASSERT_TRUE(result == RPMTAG_VERSION);
    json_object_put(entry);

    /* test with entry missing tag field */
    entry = json_object_new_object();
    json_object_object_add(entry, "notag", json_object_new_string("Name"));
    result = get_tag_number(entry, false);
    TARPM_ASSERT_TRUE(result == RPMTAG_NOT_FOUND);
    json_object_put(entry);

    return;
}

void
test_get_tag_value(void)
{
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    const char *result = NULL;

    /* NULL inputs return NULL */
    result = get_tag_value(NULL, "Name");
    TARPM_ASSERT_TRUE(result == NULL);

    result = get_tag_value(json_object_new_array(), NULL);
    TARPM_ASSERT_TRUE(result == NULL);

    /* create a tags array with some entries */
    tags = json_object_new_array();

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Version"));
    json_object_object_add(entry, "value", json_object_new_string("1.0"));
    json_object_array_add(tags, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Release"));
    json_object_object_add(entry, "value", json_object_new_string("1"));
    json_object_array_add(tags, entry);

    /* test getting valid tag values */
    result = get_tag_value(tags, "Name");
    TARPM_ASSERT_TRUE(result != NULL);
    TARPM_ASSERT_TRUE(strcmp(result, "testpkg") == 0);

    result = get_tag_value(tags, "Version");
    TARPM_ASSERT_TRUE(result != NULL);
    TARPM_ASSERT_TRUE(strcmp(result, "1.0") == 0);

    result = get_tag_value(tags, "Release");
    TARPM_ASSERT_TRUE(result != NULL);
    TARPM_ASSERT_TRUE(strcmp(result, "1") == 0);

    /* test getting non-existent tag */
    result = get_tag_value(tags, "NonExistent");
    TARPM_ASSERT_TRUE(result == NULL);

    json_object_put(tags);

    return;
}

void
test_set_tag_value(void)
{
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    const char *result = NULL;
    int ret = 0;

    /* NULL inputs return -1 */
    ret = set_tag_value(NULL, "Name", "newvalue");
    TARPM_ASSERT_TRUE(ret == -1);

    tags = json_object_new_array();
    ret = set_tag_value(tags, NULL, "newvalue");
    TARPM_ASSERT_TRUE(ret == -1);

    ret = set_tag_value(tags, "Name", NULL);
    TARPM_ASSERT_TRUE(ret == -1);
    json_object_put(tags);

    /* create a tags array with some entries */
    tags = json_object_new_array();

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Version"));
    json_object_object_add(entry, "value", json_object_new_string("1.0"));
    json_object_array_add(tags, entry);

    /* test setting an existing tag value */
    ret = set_tag_value(tags, "Name", "newname");
    TARPM_ASSERT_TRUE(ret == 0);

    result = get_tag_value(tags, "Name");
    TARPM_ASSERT_TRUE(result != NULL);
    TARPM_ASSERT_TRUE(strcmp(result, "newname") == 0);

    /* test setting another existing tag value */
    ret = set_tag_value(tags, "Version", "2.0");
    TARPM_ASSERT_TRUE(ret == 0);

    result = get_tag_value(tags, "Version");
    TARPM_ASSERT_TRUE(result != NULL);
    TARPM_ASSERT_TRUE(strcmp(result, "2.0") == 0);

    /* test setting non-existent tag (should fail) */
    ret = set_tag_value(tags, "NonExistent", "value");
    TARPM_ASSERT_TRUE(ret == -1);

    json_object_put(tags);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("tags", init_test_tags, clean_test_tags);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test strtagtype()", test_strtagtype) == NULL ||
        CU_add_test(pSuite, "test sig_tag_name()", test_sig_tag_name) == NULL ||
        CU_add_test(pSuite, "test tag_type()", test_tag_type) == NULL ||
        CU_add_test(pSuite, "test get_tag_number()", test_get_tag_number) == NULL ||
        CU_add_test(pSuite, "test get_tag_value()", test_get_tag_value) == NULL ||
        CU_add_test(pSuite, "test set_tag_value()", test_set_tag_value) == NULL) {
        return NULL;
    }

    return pSuite;
}
