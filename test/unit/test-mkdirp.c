/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_mkdirp(void)
{
    return 0;
}

int
clean_test_mkdirp(void)
{
    return 0;
}

void
test_mkdirp(void)
{
    TARPM_ASSERT_TRUE(mkdirp(NULL, 0) == -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("mkdirp", init_test_mkdirp, clean_test_mkdirp);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test mkdirp()", test_mkdirp) == NULL) {
        return NULL;
    }

    return pSuite;
}
