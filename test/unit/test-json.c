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
    char tmpfile[] = "/tmp/tarpm-test-json-XXXXXX";
    int fd = -1;
    struct json_object *data = NULL;
    struct json_object *value = NULL;

    TARPM_ASSERT_TRUE(write_json_file(NULL, NULL) == -1);

    data = json_object_new_object();
    json_object_object_add(data, "name", json_object_new_string("tarpm"));

    /* a missing path is an error */
    TARPM_ASSERT_TRUE(write_json_file(data, NULL) == -1);

    /* the JSON lands in the file named */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(close(fd) == 0);

    TARPM_ASSERT_TRUE(write_json_file(data, tmpfile) == 0);

    json_object_put(data);

    data = read_json_file(tmpfile);
    TARPM_ASSERT_PTR_NOT_NULL(data);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(data, "name", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "tarpm");

    json_object_put(data);
    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    return;
}

void
test_write_json_file_stdout(void)
{
    char tmpfile[] = "/tmp/tarpm-test-json-XXXXXX";
    char buf[BUFSIZ];
    int fd = -1;
    int saved = -1;
    size_t len = 0;
    FILE *fp = NULL;
    struct json_object *data = NULL;

    TARPM_ASSERT_TRUE(write_json_file(NULL, OUTPUT_STDOUT) == -1);

    /* catch what goes to stdout in a temporary file */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);

    saved = dup(STDOUT_FILENO);
    TARPM_ASSERT_FALSE(saved == -1);
    TARPM_ASSERT_TRUE(fflush(stdout) == 0);
    TARPM_ASSERT_FALSE(dup2(fd, STDOUT_FILENO) == -1);

    data = json_object_new_object();
    json_object_object_add(data, "name", json_object_new_string("tarpm"));

    TARPM_ASSERT_TRUE(write_json_file(data, OUTPUT_STDOUT) == 0);

    json_object_put(data);

    /* put stdout back the way it was */
    TARPM_ASSERT_FALSE(dup2(saved, STDOUT_FILENO) == -1);
    TARPM_ASSERT_TRUE(close(saved) == 0);
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* the JSON landed on stdout rather than in a file named "-" */
    memset(buf, '\0', sizeof(buf));
    fp = fopen(tmpfile, "r");
    TARPM_ASSERT_PTR_NOT_NULL(fp);
    len = fread(buf, 1, sizeof(buf) - 1, fp);
    TARPM_ASSERT_TRUE(len > 0);
    TARPM_ASSERT_TRUE(fclose(fp) == 0);

    TARPM_ASSERT_TRUE(strstr(buf, "\"name\"") != NULL);
    TARPM_ASSERT_TRUE(strstr(buf, "\"tarpm\"") != NULL);

    TARPM_ASSERT_FALSE(access(OUTPUT_STDOUT, F_OK) == 0);

    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

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

    /*
     * update_signature() works the digests out again when we create a
     * package, so they cannot be edited by the user and come out
     * read-only like the rest of the signature.
     */
    hdrentry.tag = htonl(RPMSIGTAG_MD5);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(16);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    hdrentry.tag = htonl(RPMSIGTAG_SHA1);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    hdrentry.tag = htonl(RPMSIGTAG_SHA256);
    hdrentry.type = htonl(RPM_STRING_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    return;
}

void
test_create_json_entry_signature_size_tags(void)
{
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    /*
     * update_signature() works the sizes out again when we create a
     * package, so they are read-only like the rest of the signature.
     */
    hdrentry.tag = htonl(RPMSIGTAG_SIZE);
    hdrentry.type = htonl(RPM_INT32_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    hdrentry.tag = htonl(RPMSIGTAG_LONGSIZE);
    hdrentry.type = htonl(RPM_INT64_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    hdrentry.tag = htonl(RPMSIGTAG_PAYLOADSIZE);
    hdrentry.type = htonl(RPM_INT32_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
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
test_create_json_entry_signature_file_tags(void)
{
    int i = 0;
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;
    struct {
        uint32_t tag;
        const char *name;
    } tags[] = {
        { RPMSIGTAG_PUBKEYS_VALUE, "Pubkeys" },
        { RPMSIGTAG_FILESIGNATURES_VALUE, "Filesignatures" },
        { RPMSIGTAG_FILESIGNATURELENGTH_VALUE, "Filesignaturelength" },
        { RPMSIGTAG_VERITYSIGNATURES_VALUE, "Veritysignatures" },
        { RPMSIGTAG_VERITYSIGNATUREALGO_VALUE, "Veritysignaturealgo" },
        { RPMSIGTAG_OPENPGP_VALUE, "Openpgp" },
        { RPMSIGTAG_SHA3_256_VALUE, "Sha3_256" },
        { 0, NULL }
    };

    /*
     * The file signing and verity tags get a name rather than a
     * number, and like everything else in the signature header they
     * are read-only.
     */
    for (i = 0; tags[i].name != NULL; i++) {
        hdrentry.tag = htonl(tags[i].tag);
        hdrentry.type = htonl(RPM_BIN_TYPE);
        hdrentry.offset = htonl(0);
        hdrentry.count = htonl(1);

        entry = create_json_entry(&hdrentry, true);
        TARPM_ASSERT_PTR_NOT_NULL(entry);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "tag", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), tags[i].name);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
        json_object_put(entry);
    }

    /*
     * The reserved space tag is carried over to a package we create
     * rather than worked out again, but it is still read-only.
     */
    hdrentry.tag = htonl(RPMSIGTAG_RESERVED_VALUE);
    hdrentry.type = htonl(RPM_BIN_TYPE);
    hdrentry.offset = htonl(0);
    hdrentry.count = htonl(1);

    entry = create_json_entry(&hdrentry, true);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "tag", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "Reserved");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
    json_object_put(entry);

    return;
}

void
test_create_json_entry_signature_all_read_only(void)
{
    int i = 0;
    struct rpmhdrentry hdrentry;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;
    uint32_t tags[] = {
        HEADER_SIGNATURES,
        RPMSIGTAG_BADSHA1_1,
        RPMSIGTAG_BADSHA1_2,
        RPMSIGTAG_PUBKEYS_VALUE,
        RPMSIGTAG_DSA,
        RPMSIGTAG_RSA,
        RPMSIGTAG_SHA1,
        RPMSIGTAG_LONGSIZE,
        RPMSIGTAG_LONGARCHIVESIZE,
        RPMSIGTAG_SHA256,
        RPMSIGTAG_FILESIGNATURES_VALUE,
        RPMSIGTAG_FILESIGNATURELENGTH_VALUE,
        RPMSIGTAG_VERITYSIGNATURES_VALUE,
        RPMSIGTAG_VERITYSIGNATUREALGO_VALUE,
        RPMSIGTAG_OPENPGP_VALUE,
        RPMSIGTAG_SHA3_256_VALUE,
        RPMSIGTAG_RESERVED_VALUE,
        RPMSIGTAG_SIZE,
        RPMSIGTAG_LEMD5_1,
        RPMSIGTAG_PGP,
        RPMSIGTAG_LEMD5_2,
        RPMSIGTAG_MD5,
        RPMSIGTAG_GPG,
        RPMSIGTAG_PGP5,
        RPMSIGTAG_PAYLOADSIZE,
        RPMSIGTAG_RESERVEDSPACE,
        0
    };

    /* all tags in the signature header are read-only to the user */
    for (i = 0; tags[i] != 0; i++) {
        hdrentry.tag = htonl(tags[i]);
        hdrentry.type = htonl(RPM_BIN_TYPE);
        hdrentry.offset = htonl(0);
        hdrentry.count = htonl(1);

        entry = create_json_entry(&hdrentry, true);
        TARPM_ASSERT_PTR_NOT_NULL(entry);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "read-only", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "true");
        json_object_put(entry);

        /* the same number in the main header is not read-only */
        entry = create_json_entry(&hdrentry, false);
        TARPM_ASSERT_PTR_NOT_NULL(entry);
        TARPM_ASSERT_FALSE(json_object_object_get_ex(entry, "read-only", &value));
        json_object_put(entry);
    }

    return;
}

void
test_generate_json_entries_signature_order(void)
{
    int i = 0;
    uint32_t data[4];
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[4];
    struct json_object *kvals = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;
    /* the tag numbers above in the order we expect to read them back */
    uint32_t tags[] = {
        RPMSIGTAG_PAYLOADSIZE,
        RPMSIGTAG_SIZE,
        RPMSIGTAG_VERITYSIGNATUREALGO_VALUE,
        RPMSIGTAG_FILESIGNATURELENGTH_VALUE
    };
    const char *names[] = {
        "Filesignaturelength",
        "Veritysignaturealgo",
        "Size",
        "Payloadsize"
    };

    /*
     * Build a signature header with the tags out of order so we can
     * see that we sort them by signature tag number and not by the
     * header tag number that shares the same name.
     */
    memset(&hdr, 0, sizeof(hdr));
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(data, 0, sizeof(data));

    hdr.nentries = 4;

    for (i = 0; i < 4; i++) {
        entries[i].tag = htonl(tags[i]);
        entries[i].type = htonl(RPM_INT32_TYPE);
        entries[i].offset = htonl(i * (int32_t) sizeof(uint32_t));
        entries[i].count = htonl(1);
        data[i] = htonl(i + 1);
    }

    hdrinfo.estart = entries;
    hdrinfo.datastart = (uint8_t *) data;

    kvals = generate_json_entries(&hdr, &hdrinfo, NULL, NULL, true);
    TARPM_ASSERT_PTR_NOT_NULL(kvals);
    TARPM_ASSERT_EQUAL(json_object_array_length(kvals), 4);

    for (i = 0; i < 4; i++) {
        entry = json_object_array_get_idx(kvals, i);
        TARPM_ASSERT_PTR_NOT_NULL(entry);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, "tag", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), names[i]);
    }

    json_object_put(kvals);

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
        CU_add_test(pSuite, "test write_json_file() to stdout", test_write_json_file_stdout) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with NULL", test_create_json_entry_null) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with non-signature", test_create_json_entry_non_signature) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature digest tags", test_create_json_entry_signature_digest_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature size tags", test_create_json_entry_signature_size_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature crypto tags", test_create_json_entry_signature_crypto_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() with signature file tags", test_create_json_entry_signature_file_tags) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() marks all signature tags read-only", test_create_json_entry_signature_all_read_only) == NULL ||
        CU_add_test(pSuite, "test generate_json_entries() signature order", test_generate_json_entries_signature_order) == NULL ||
        CU_add_test(pSuite, "test create_json_entry() has required fields", test_create_json_entry_has_required_fields) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with a missing file", test_read_json_file_missing) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with invalid JSON", test_read_json_file_invalid) == NULL ||
        CU_add_test(pSuite, "test read_json_file() with valid JSON", test_read_json_file_valid) == NULL) {
        return NULL;
    }

    return pSuite;
}
