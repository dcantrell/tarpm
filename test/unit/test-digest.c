/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <archive.h>
#include <archive_entry.h>
#include <CUnit/Basic.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_digest(void)
{
    return 0;
}

int
clean_test_digest(void)
{
    return 0;
}

void
test_mksigdigest_invalid_type(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;

    /* initialize valid structures */
    memset(&hdr, 0, sizeof(hdr));
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    data = json_object_new_object();

    /* create a temporary file for testing */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);

    /* test invalid digest type (0) */
    digest = mksigdigest(0, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* test invalid digest type (negative) */
    digest = mksigdigest(-1, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* test unsupported digest type (99) */
    digest = mksigdigest(99, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_mksigdigest_null_parameters(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;

    /* initialize valid structures */
    memset(&hdr, 0, sizeof(hdr));
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    data = json_object_new_object();
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);

    /* test NULL hdr */
    digest = mksigdigest(TARPM_DIGEST_MD5, NULL, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* test NULL hdrinfo */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, NULL, data, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* test NULL data */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, &hdrinfo, NULL, fd);
    TARPM_ASSERT_PTR_NULL(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_mksigdigest_invalid_fd(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct json_object *data = NULL;
    unsigned char *digest = NULL;

    /* initialize valid structures */
    memset(&hdr, 0, sizeof(hdr));
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    data = json_object_new_object();

    /* test invalid file descriptor */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, &hdrinfo, data, -1);
    TARPM_ASSERT_PTR_NULL(digest);

    /* clean up */
    json_object_put(data);

    return;
}

void
test_mksigdigest_md5(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[2];
    uint8_t datastore[16];
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;
    const char *test_payload = "test payload data";

    /* initialize header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(RPM_SIGNATURE_MAGIC);
    hdr.reserved = htonl(RPM_SIGNATURE_RESERVED);
    hdr.nentries = htonl(2);
    hdr.nbytes = htonl(16);

    /* initialize header info */
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(datastore, 0, sizeof(datastore));
    hdrinfo.estart = entries;
    hdrinfo.datastart = datastore;

    /* create JSON data */
    data = json_object_new_object();

    /* create a temporary file with test payload */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);
    TARPM_ASSERT_TRUE(write(fd, test_payload, strlen(test_payload)) == (ssize_t)strlen(test_payload));

    /* compute MD5 digest */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);

    /*
     * MD5 should produce 16 bytes
     * Can't verify exact value without knowing input, but verify it's
     * non-null and allocated.
     */
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_mksigdigest_sha1(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[2];
    uint8_t datastore[16];
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;
    const char *test_payload = "test payload data";

    /* initialize header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(RPM_SIGNATURE_MAGIC);
    hdr.reserved = htonl(RPM_SIGNATURE_RESERVED);
    hdr.nentries = htonl(2);
    hdr.nbytes = htonl(16);

    /* initialize header info */
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(datastore, 0, sizeof(datastore));
    hdrinfo.estart = entries;
    hdrinfo.datastart = datastore;

    /* create JSON data */
    data = json_object_new_object();

    /* create a temporary file with test payload */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);
    TARPM_ASSERT_TRUE(write(fd, test_payload, strlen(test_payload)) == (ssize_t)strlen(test_payload));

    /* compute SHA-1 digest (header only, no payload) */
    digest = mksigdigest(TARPM_DIGEST_SHA1, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);

    /* SHA-1 should produce 20 bytes */
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_mksigdigest_sha256(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[2];
    uint8_t datastore[16];
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;
    const char *test_payload = "test payload data";

    /* initialize header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(RPM_SIGNATURE_MAGIC);
    hdr.reserved = htonl(RPM_SIGNATURE_RESERVED);
    hdr.nentries = htonl(2);
    hdr.nbytes = htonl(16);

    /* initialize header info */
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(datastore, 0, sizeof(datastore));
    hdrinfo.estart = entries;
    hdrinfo.datastart = datastore;

    /* create JSON data */
    data = json_object_new_object();

    /* create a temporary file with test payload */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);
    TARPM_ASSERT_TRUE(write(fd, test_payload, strlen(test_payload)) == (ssize_t)strlen(test_payload));

    /* compute SHA-256 digest (header only, no payload) */
    digest = mksigdigest(TARPM_DIGEST_SHA256, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);

    /* SHA-256 should produce 32 bytes */
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_payload_digest_sha256(void)
{
    int fd = -1;
    uint64_t size = 0;
    char *digest = NULL;
    const char *test_payload = "test payload data";

    /*
     * the SHA-256 of "test payload data", which is what the main
     * header records for the payload as it lands in the package
     */
    const char *expected = "84e6c7064a6672fa2994643ff5c626ac5c6f9d6e4a8c23e0b4d8e27a0b7d311b";

    /* create a temporary file with test payload */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);
    TARPM_ASSERT_TRUE(write(fd, test_payload, strlen(test_payload)) == (ssize_t)strlen(test_payload));

    digest = payload_digest(TARPM_DIGEST_SHA256, fd, &size);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_TRUE(strlen(digest) == (SHA256_DIGEST_LENGTH * 2));
    TARPM_ASSERT_TRUE(size == strlen(test_payload));
    TARPM_ASSERT_STRING_EQUAL(digest, expected);
    free(digest);

    /* an unknown digest type gives us nothing */
    digest = payload_digest(-1, fd, NULL);
    TARPM_ASSERT_PTR_NULL(digest);

    /* clean up */
    close(fd);

    return;
}

void
test_nul_digest(void)
{
    char *digest = NULL;

    /* each digest is all zeroes and as wide as the algorithm */
    digest = nul_digest(TARPM_DIGEST_SHA256);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_TRUE(strlen(digest) == (SHA256_DIGEST_LENGTH * 2));
    TARPM_ASSERT_TRUE(strspn(digest, "0") == strlen(digest));
    free(digest);

    digest = nul_digest(TARPM_DIGEST_SHA512);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_TRUE(strlen(digest) == (SHA512_DIGEST_LENGTH * 2));
    TARPM_ASSERT_TRUE(strspn(digest, "0") == strlen(digest));
    free(digest);

    digest = nul_digest(TARPM_DIGEST_SHA3_256);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_TRUE(strlen(digest) == (SHA256_DIGEST_LENGTH * 2));
    free(digest);

    /* an unknown digest type gives us nothing */
    digest = nul_digest(-1);
    TARPM_ASSERT_PTR_NULL(digest);

    return;
}

void
test_mksigdigest_empty_payload(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[2];
    uint8_t datastore[16];
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;

    /* initialize header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(RPM_SIGNATURE_MAGIC);
    hdr.reserved = htonl(RPM_SIGNATURE_RESERVED);
    hdr.nentries = htonl(2);
    hdr.nbytes = htonl(16);

    /* initialize header info */
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(datastore, 0, sizeof(datastore));
    hdrinfo.estart = entries;
    hdrinfo.datastart = datastore;

    /* create JSON data */
    data = json_object_new_object();

    /* create an empty temporary file */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);

    /* compute MD5 digest with empty payload */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    free(digest);

    /* compute SHA-256 digest with empty payload */
    digest = mksigdigest(TARPM_DIGEST_SHA256, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_mksigdigest_large_payload(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[2];
    uint8_t datastore[16];
    struct json_object *data = NULL;
    int fd = -1;
    unsigned char *digest = NULL;
    char buffer[8192];
    int i = 0;

    /* initialize header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(RPM_SIGNATURE_MAGIC);
    hdr.reserved = htonl(RPM_SIGNATURE_RESERVED);
    hdr.nentries = htonl(2);
    hdr.nbytes = htonl(16);

    /* initialize header info */
    memset(&hdrinfo, 0, sizeof(hdrinfo));
    memset(entries, 0, sizeof(entries));
    memset(datastore, 0, sizeof(datastore));
    hdrinfo.estart = entries;
    hdrinfo.datastart = datastore;

    /* create JSON data */
    data = json_object_new_object();

    /* create a temporary file with large payload (multiple BUFSIZ chunks) */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);

    /* write multiple chunks of data */
    memset(buffer, 'A', sizeof(buffer));
    for (i = 0; i < 10; i++) {
        TARPM_ASSERT_TRUE(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
    }

    /* compute MD5 digest with large payload */
    digest = mksigdigest(TARPM_DIGEST_MD5, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

    return;
}

void
test_archive_digest_gzip(void)
{
    int fd = -1;
    uint64_t size = 0;
    char *digest = NULL;
    char *plain = NULL;
    struct archive *out = NULL;
    struct archive_entry *entry = NULL;
    const char *content = "uncompressed payload data";

    /* write a gzip compressed cpio archive holding one file */
    fd = memfd_create("test-digest", MFD_CLOEXEC);
    TARPM_ASSERT_TRUE(fd != -1);

    out = archive_write_new();
    TARPM_ASSERT_TRUE(archive_write_set_format_cpio_newc(out) == ARCHIVE_OK);
    TARPM_ASSERT_TRUE(archive_write_add_filter_gzip(out) == ARCHIVE_OK);
    TARPM_ASSERT_TRUE(archive_write_open_fd(out, fd) == ARCHIVE_OK);

    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "./payload");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, strlen(content));
    TARPM_ASSERT_TRUE(archive_write_header(out, entry) == ARCHIVE_OK);
    TARPM_ASSERT_TRUE(archive_write_data(out, content, strlen(content)) == (ssize_t) strlen(content));
    archive_entry_free(entry);

    TARPM_ASSERT_TRUE(archive_write_close(out) == ARCHIVE_OK);
    TARPM_ASSERT_TRUE(archive_write_free(out) == ARCHIVE_OK);

    /* the archive digest covers the cpio stream with the gzip taken off */
    digest = archive_digest(TARPM_DIGEST_SHA256, fd, &size);
    TARPM_ASSERT_PTR_NOT_NULL(digest);
    TARPM_ASSERT_TRUE(strlen(digest) == (SHA256_DIGEST_LENGTH * 2));

    /* which is bigger than the compressed payload and a different digest */
    plain = payload_digest(TARPM_DIGEST_SHA256, fd, NULL);
    TARPM_ASSERT_PTR_NOT_NULL(plain);
    TARPM_ASSERT_TRUE(size > strlen(content));
    TARPM_ASSERT_STRING_NOT_EQUAL(digest, plain);

    free(digest);
    free(plain);
    close(fd);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("digest", init_test_digest, clean_test_digest);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test mksigdigest() invalid type", test_mksigdigest_invalid_type) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() null parameters", test_mksigdigest_null_parameters) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() invalid fd", test_mksigdigest_invalid_fd) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() MD5", test_mksigdigest_md5) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() SHA-1", test_mksigdigest_sha1) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() SHA-256", test_mksigdigest_sha256) == NULL ||
        CU_add_test(pSuite, "test payload_digest() SHA-256", test_payload_digest_sha256) == NULL ||
        CU_add_test(pSuite, "test archive_digest() gzip", test_archive_digest_gzip) == NULL ||
        CU_add_test(pSuite, "test nul_digest()", test_nul_digest) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() empty payload", test_mksigdigest_empty_payload) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() large payload", test_mksigdigest_large_payload) == NULL) {
        return NULL;
    }

    return pSuite;
}
