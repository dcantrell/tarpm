/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
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
test_valid_header_signature(void)
{
    struct rpmhdr hdr;

    /* NULL input returns false */
    TARPM_ASSERT_FALSE(valid_header_signature(NULL));

    /* invalid magic returns false */
    hdr.magic = 0x12345678;
    hdr.reserved = RPM_SIGNATURE_RESERVED;
    TARPM_ASSERT_FALSE(valid_header_signature(&hdr));

    /* invalid reserved returns false */
    hdr.magic = RPM_SIGNATURE_MAGIC;
    hdr.reserved = 0x12345678;
    TARPM_ASSERT_FALSE(valid_header_signature(&hdr));

    /* valid magic and reserved returns true */
    hdr.magic = RPM_SIGNATURE_MAGIC;
    hdr.reserved = RPM_SIGNATURE_RESERVED;
    TARPM_ASSERT_TRUE(valid_header_signature(&hdr));

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
        CU_add_test(pSuite, "test valid_header_signature()", test_valid_header_signature) == NULL ||
        CU_add_test(pSuite, "test has_trailer()", test_has_trailer) == NULL) {
        return NULL;
    }

    return pSuite;
}
