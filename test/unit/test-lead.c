/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_lead(void)
{
    return 0;
}

int
clean_test_lead(void)
{
    return 0;
}

void
test_extract_lead(void)
{
    TARPM_ASSERT_TRUE(extract_lead(-47, NULL) == -1);
    TARPM_ASSERT_TRUE(extract_lead(0, NULL) == -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("lead", init_test_lead, clean_test_lead);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test extract_lead()", test_extract_lead) == NULL) {
        return NULL;
    }

    return pSuite;
}
