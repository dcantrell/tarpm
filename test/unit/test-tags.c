/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmpgp.h>
#include "tarpm.h"

#include "test-main.h"

/*
 * Every signature header tag we know a name for.  The zero on the end
 * marks the end of the list.
 */
static uint32_t sig_tags[] = {
    HEADER_SIGNATURES,
    HEADER_IMMUTABLE,
    RPMSIGTAG_SIZE,
    RPMSIGTAG_LEMD5_1,
    RPMSIGTAG_PGP,
    RPMSIGTAG_LEMD5_2,
    RPMSIGTAG_MD5,
    RPMSIGTAG_GPG,
    RPMSIGTAG_PGP5,
    RPMSIGTAG_PAYLOADSIZE,
    RPMSIGTAG_RESERVEDSPACE,
    RPMSIGTAG_BADSHA1_1,
    RPMSIGTAG_BADSHA1_2,
    RPMSIGTAG_DSA,
    RPMSIGTAG_RSA,
    RPMSIGTAG_SHA1,
    RPMSIGTAG_LONGSIZE,
    RPMSIGTAG_LONGARCHIVESIZE,
    RPMSIGTAG_SHA256,
    RPMSIGTAG_PUBKEYS_VALUE,
    RPMSIGTAG_FILESIGNATURES_VALUE,
    RPMSIGTAG_FILESIGNATURELENGTH_VALUE,
    RPMSIGTAG_VERITYSIGNATURES_VALUE,
    RPMSIGTAG_VERITYSIGNATUREALGO_VALUE,
    RPMSIGTAG_OPENPGP_VALUE,
    RPMSIGTAG_SHA3_256_VALUE,
    RPMSIGTAG_RESERVED_VALUE,
    0
};

/*
 * Every file digest algorithm we know a name for.  The NULL name on
 * the end marks the end of the list.
 */
static struct {
    uint32_t algo;
    const char *name;
} digest_algos[] = {
    { PGPHASHALGO_MD5, "md5" },
    { PGPHASHALGO_SHA1, "sha1" },
    { PGPHASHALGO_RIPEMD160, "ripemd160" },
    { PGPHASHALGO_MD2, "md2" },
    { PGPHASHALGO_TIGER192, "tiger192" },
    { PGPHASHALGO_HAVAL_5_160, "haval-5-160" },
    { PGPHASHALGO_SHA256, "sha256" },
    { PGPHASHALGO_SHA384, "sha384" },
    { PGPHASHALGO_SHA512, "sha512" },
    { PGPHASHALGO_SHA224, "sha224" },
    { PGPHASHALGO_SHA3_256_VALUE, "sha3-256" },
    { PGPHASHALGO_SHA3_512_VALUE, "sha3-512" },
    { 0, NULL }
};

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
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PUBKEYS_VALUE), "Pubkeys") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURES_VALUE), "Filesignatures") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURELENGTH_VALUE), "Filesignaturelength") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATURES_VALUE), "Veritysignatures") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATUREALGO_VALUE), "Veritysignaturealgo") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_OPENPGP_VALUE), "Openpgp") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SHA3_256_VALUE), "Sha3_256") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_RESERVED_VALUE), "Reserved") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(0), "(unknown)") == 0);

    /*
     * The tag numbers we carry ourselves have to agree with the ones
     * the rpm we build against uses.
     */
    TARPM_ASSERT_TRUE(RPMSIGTAG_PUBKEYS_VALUE == RPMTAG_PUBKEYS);
    TARPM_ASSERT_TRUE(RPMSIGTAG_VERITYSIGNATURES_VALUE == RPMTAG_VERITYSIGNATURES);
    TARPM_ASSERT_TRUE(RPMSIGTAG_VERITYSIGNATUREALGO_VALUE == RPMTAG_VERITYSIGNATUREALGO);

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
    int i = 0;
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

    /*
     * Every signature tag name we write has to read back as the same
     * number we wrote it for.
     */
    for (i = 0; sig_tags[i] != 0; i++) {
        entry = json_object_new_object();
        json_object_object_add(entry, "tag", json_object_new_string(sig_tag_name(sig_tags[i])));
        result = get_tag_number(entry, true);
        TARPM_ASSERT_TRUE(result == (rpmTagVal) sig_tags[i]);
        json_object_put(entry);
    }

    /*
     * Some signature tags share a name with a header tag that carries
     * a different number, so the signature flag has to pick the right
     * one.
     */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Filesignatures"));
    TARPM_ASSERT_TRUE(get_tag_number(entry, true) == RPMSIGTAG_FILESIGNATURES_VALUE);
    TARPM_ASSERT_TRUE(get_tag_number(entry, false) == RPMTAG_FILESIGNATURES);
    json_object_put(entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Size"));
    TARPM_ASSERT_TRUE(get_tag_number(entry, true) == RPMSIGTAG_SIZE);
    TARPM_ASSERT_TRUE(get_tag_number(entry, false) == RPMTAG_SIZE);
    json_object_put(entry);

    /* tags we have no name for come back as a number */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("#272"));
    result = get_tag_number(entry, true);
    TARPM_ASSERT_TRUE(result == 272);
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

void
test_strdigestalgo(void)
{
    int i = 0;
    char *s = NULL;

    /* algorithms tarpm knows about come back by name */
    s = strdigestalgo(PGPHASHALGO_MD5);
    TARPM_ASSERT_STRING_EQUAL(s, "md5");
    free(s);

    s = strdigestalgo(PGPHASHALGO_SHA1);
    TARPM_ASSERT_STRING_EQUAL(s, "sha1");
    free(s);

    s = strdigestalgo(PGPHASHALGO_SHA256);
    TARPM_ASSERT_STRING_EQUAL(s, "sha256");
    free(s);

    s = strdigestalgo(PGPHASHALGO_SHA512);
    TARPM_ASSERT_STRING_EQUAL(s, "sha512");
    free(s);

    /* the SHA3 algorithms get a name and not a number */
    s = strdigestalgo(PGPHASHALGO_SHA3_256_VALUE);
    TARPM_ASSERT_STRING_EQUAL(s, "sha3-256");
    free(s);

    s = strdigestalgo(PGPHASHALGO_SHA3_512_VALUE);
    TARPM_ASSERT_STRING_EQUAL(s, "sha3-512");
    free(s);

    /* every algorithm we know gets its own name */
    for (i = 0; digest_algos[i].name != NULL; i++) {
        s = strdigestalgo(digest_algos[i].algo);
        TARPM_ASSERT_STRING_EQUAL(s, digest_algos[i].name);
        free(s);
    }

    /* anything else comes back as the number itself */
    s = strdigestalgo(0);
    TARPM_ASSERT_STRING_EQUAL(s, "0");
    free(s);

    s = strdigestalgo(47);
    TARPM_ASSERT_STRING_EQUAL(s, "47");
    free(s);

    return;
}

void
test_digest_algo(void)
{
    int i = 0;
    char *s = NULL;

    /* names come back as the algorithm they were written from */
    TARPM_ASSERT_EQUAL(digest_algo("md5"), PGPHASHALGO_MD5);
    TARPM_ASSERT_EQUAL(digest_algo("sha1"), PGPHASHALGO_SHA1);
    TARPM_ASSERT_EQUAL(digest_algo("sha256"), PGPHASHALGO_SHA256);
    TARPM_ASSERT_EQUAL(digest_algo("sha512"), PGPHASHALGO_SHA512);
    TARPM_ASSERT_EQUAL(digest_algo("sha3-256"), PGPHASHALGO_SHA3_256_VALUE);
    TARPM_ASSERT_EQUAL(digest_algo("sha3-512"), PGPHASHALGO_SHA3_512_VALUE);

    /* every algorithm we write has to read back the same way */
    for (i = 0; digest_algos[i].name != NULL; i++) {
        s = strdigestalgo(digest_algos[i].algo);
        TARPM_ASSERT_EQUAL(digest_algo(s), digest_algos[i].algo);
        free(s);
    }

    /*
     * The numbers we carry ourselves have to agree with the ones the
     * rpm we build against uses.
     */
    TARPM_ASSERT_EQUAL(PGPHASHALGO_SHA3_256_VALUE, PGPHASHALGO_SHA3_256);
    TARPM_ASSERT_EQUAL(PGPHASHALGO_SHA3_512_VALUE, PGPHASHALGO_SHA3_512);

    /* older files record the SHA3 algorithms as a bare number */
    TARPM_ASSERT_EQUAL(digest_algo("12"), PGPHASHALGO_SHA3_256_VALUE);
    TARPM_ASSERT_EQUAL(digest_algo("14"), PGPHASHALGO_SHA3_512_VALUE);

    /* a bare number is read as the algorithm itself */
    TARPM_ASSERT_EQUAL(digest_algo("47"), 47);

    /* anything else is zero */
    TARPM_ASSERT_EQUAL(digest_algo(NULL), 0);
    TARPM_ASSERT_EQUAL(digest_algo(""), 0);
    TARPM_ASSERT_EQUAL(digest_algo("not an algorithm"), 0);

    return;
}

void
test_strbuildtime(void)
{
    char *s = NULL;

    /* the epoch itself */
    s = strbuildtime(0);
    TARPM_ASSERT_PTR_NOT_NULL(s);
    TARPM_ASSERT_TRUE(strcmp(s, "1970-01-01T00:00:00Z") == 0);
    free(s);

    /* a real build time */
    s = strbuildtime(1753056000);
    TARPM_ASSERT_PTR_NOT_NULL(s);
    TARPM_ASSERT_TRUE(strcmp(s, "2025-07-21T00:00:00Z") == 0);
    free(s);

    /* the last build time an unsigned 32 bit value can hold */
    s = strbuildtime(4294967295U);
    TARPM_ASSERT_PTR_NOT_NULL(s);
    TARPM_ASSERT_TRUE(strcmp(s, "2106-02-07T06:28:15Z") == 0);
    free(s);

    return;
}

void
test_buildtime_value(void)
{
    /* timestamps come back as the value they were written from */
    TARPM_ASSERT_EQUAL(buildtime_value("1970-01-01T00:00:00Z"), 0);
    TARPM_ASSERT_EQUAL(buildtime_value("2025-07-21T00:00:00Z"), 1753056000);
    TARPM_ASSERT_EQUAL(buildtime_value("2106-02-07T06:28:15Z"), 4294967295U);

    /* a bare number is read as the value itself */
    TARPM_ASSERT_EQUAL(buildtime_value("1753056000"), 1753056000);

    /* anything else is zero */
    TARPM_ASSERT_EQUAL(buildtime_value(NULL), 0);
    TARPM_ASSERT_EQUAL(buildtime_value(""), 0);
    TARPM_ASSERT_EQUAL(buildtime_value("not a timestamp"), 0);

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
        CU_add_test(pSuite, "test set_tag_value()", test_set_tag_value) == NULL ||
        CU_add_test(pSuite, "test strdigestalgo()", test_strdigestalgo) == NULL ||
        CU_add_test(pSuite, "test digest_algo()", test_digest_algo) == NULL ||
        CU_add_test(pSuite, "test strbuildtime()", test_strbuildtime) == NULL ||
        CU_add_test(pSuite, "test buildtime_value()", test_buildtime_value) == NULL) {
        return NULL;
    }

    return pSuite;
}
