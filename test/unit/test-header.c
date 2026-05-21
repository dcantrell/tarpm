/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
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
test_read_header_from_rpm(void)
{
    /* check that invalid input returns an error */
    TARPM_ASSERT_TRUE(read_header_from_rpm(-47) == NULL);
    TARPM_ASSERT_TRUE(read_header_from_rpm(0) == NULL);

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
    if (CU_add_test(pSuite, "test read_header_from_rpm()", test_read_header_from_rpm) == NULL) {
        return NULL;
    }

    return pSuite;
}
