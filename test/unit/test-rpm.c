/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <rpm/header.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_rpm(void)
{
    return 0;
}

int
clean_test_rpm(void)
{
    return 0;
}

void
test_unpack_rpm_payload(void)
{
    TARPM_ASSERT_EQUAL(unpack_rpm_payload(NULL, NULL, false), -1);
    TARPM_ASSERT_EQUAL(unpack_rpm_payload("x.rpm", NULL, false), -1);
    TARPM_ASSERT_EQUAL(unpack_rpm_payload(NULL, "/tmp", true), -1);

    return;
}

void
test_get_header(void)
{
    TARPM_ASSERT_TRUE(get_header(NULL) == NULL);

    return;
}

void
test_get_rpmtag_str(void)
{
    char *str = NULL;
    Header h = NULL;

    TARPM_ASSERT_TRUE(get_rpmtag_str(NULL, 0) == NULL);

    h = headerNew();
    TARPM_ASSERT_TRUE(headerPutString(h, RPMTAG_NAME, "hello") == 1);
    TARPM_ASSERT_TRUE(headerPutUint32(h, RPMTAG_EPOCH, (uint32_t[]) { 3 }, 1) == 1);

    str = get_rpmtag_str(h, RPMTAG_NAME);
    TARPM_ASSERT_STRING_EQUAL(str, "hello");
    free(str);

    /* a number tag has no string value and a missing tag has no value */
    TARPM_ASSERT_TRUE(get_rpmtag_str(h, RPMTAG_EPOCH) == NULL);
    TARPM_ASSERT_TRUE(get_rpmtag_str(h, RPMTAG_VERSION) == NULL);

    headerFree(h);

    return;
}

void
test_get_header_arch(void)
{
    TARPM_ASSERT_TRUE(get_header_arch(NULL) == NULL);

    return;
}

void
test_get_nevr(void)
{
    TARPM_ASSERT_TRUE(get_nevr(NULL) == NULL);

    return;
}

void
test_get_nevra(void)
{
    TARPM_ASSERT_TRUE(get_nevra(NULL) == NULL);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("rpm", init_test_rpm, clean_test_rpm);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test unpack_rpm_payload()", test_unpack_rpm_payload) == NULL ||
        CU_add_test(pSuite, "test get_header()", test_get_header) == NULL ||
        CU_add_test(pSuite, "test get_rpmtag_str()", test_get_rpmtag_str) == NULL ||
        CU_add_test(pSuite, "test get_header_arch()", test_get_header_arch) == NULL ||
        CU_add_test(pSuite, "test get_nevr()", test_get_nevr) == NULL ||
        CU_add_test(pSuite, "test get_nevra()", test_get_nevra) == NULL) {
        return NULL;
    }

    return pSuite;
}
