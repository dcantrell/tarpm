/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

#define LOREM_IPSUM "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur."

int
init_test_strfuncs(void)
{
    return 0;
}

int
clean_test_strfuncs(void)
{
    return 0;
}

void
test_strprefix(void)
{
    TARPM_ASSERT_TRUE(strprefix("flargenblarfle", "flarg"));
    TARPM_ASSERT_FALSE(strprefix("flargenblarfle", "monkey"));
}

void
test_strsuffix(void)
{
    TARPM_ASSERT_TRUE(strsuffix("flargenblarfle", "blarfle"));
    TARPM_ASSERT_FALSE(strsuffix("flargenblarfle", "monkey"));
}

/* strappend() */

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("strfuncs", init_test_strfuncs, clean_test_strfuncs);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test strprefix()", test_strprefix) == NULL ||
        CU_add_test(pSuite, "test strsuffix()", test_strsuffix) == NULL) {
        return NULL;
    }

    return pSuite;
}
