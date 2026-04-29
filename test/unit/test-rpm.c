/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
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
test_extract_rpm_payload(void)
{
    TARPM_ASSERT_TRUE(extract_rpm_payload(NULL) == NULL);

    return;
}

void
test_get_rpm_header(void)
{
    TARPM_ASSERT_TRUE(get_rpm_header(NULL) == NULL);

    return;
}

void
test_get_rpmtag_str(void)
{
    TARPM_ASSERT_TRUE(get_rpmtag_str(NULL, 0) == NULL);

    return;
}

void
test_get_rpm_header_arch(void)
{
    TARPM_ASSERT_TRUE(get_rpm_header_arch(NULL) == NULL);

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
    if (CU_add_test(pSuite, "test extract_rpm_payload()", test_extract_rpm_payload) == NULL ||
        CU_add_test(pSuite, "test get_rpm_header()", test_get_rpm_header) == NULL ||
        CU_add_test(pSuite, "test get_rpmtag_str()", test_get_rpmtag_str) == NULL ||
        CU_add_test(pSuite, "test get_rpm_header_arch()", test_get_rpm_header_arch) == NULL ||
        CU_add_test(pSuite, "test get_nevr()", test_get_nevr) == NULL ||
        CU_add_test(pSuite, "test get_nevra()", test_get_nevra) == NULL) {
        return NULL;
    }

    return pSuite;
}
