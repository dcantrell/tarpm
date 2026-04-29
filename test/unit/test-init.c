/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_init(void)
{
    return 0;
}

int
clean_test_init(void)
{
    return 0;
}

void
test_init_librpm(void)
{
    TARPM_ASSERT_TRUE(init_librpm() == 0);
    TARPM_ASSERT_TRUE(init_librpm() == RPMRC_OK);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("init", init_test_init, clean_test_init);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test init_librpm()", test_init_librpm) == NULL) {
        return NULL;
    }

    return pSuite;
}
