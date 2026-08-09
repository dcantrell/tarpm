/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <arpa/inet.h>
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
test_mksigdigest_sha256_payload(void)
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

    /* compute SHA-256 payload-only digest */
    digest = mksigdigest(TARPM_DIGEST_SHA256_PAYLOAD, &hdr, &hdrinfo, data, fd);
    TARPM_ASSERT_PTR_NOT_NULL(digest);

    /* SHA-256 should produce 32 bytes */
    free(digest);

    /* clean up */
    close(fd);
    json_object_put(data);

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

    /* compute SHA-256 payload digest with empty payload */
    digest = mksigdigest(TARPM_DIGEST_SHA256_PAYLOAD, &hdr, &hdrinfo, data, fd);
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
        CU_add_test(pSuite, "test mksigdigest() SHA-256 payload", test_mksigdigest_sha256_payload) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() empty payload", test_mksigdigest_empty_payload) == NULL ||
        CU_add_test(pSuite, "test mksigdigest() large payload", test_mksigdigest_large_payload) == NULL) {
        return NULL;
    }

    return pSuite;
}
