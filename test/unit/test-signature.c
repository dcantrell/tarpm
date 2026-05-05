/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_signature(void)
{
    return 0;
}

int
clean_test_signature(void)
{
    return 0;
}

void
test_extract_signature(void)
{
    TARPM_ASSERT_TRUE(extract_signature(0, NULL) == -1);
    TARPM_ASSERT_TRUE(extract_signature(-47, NULL) == -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("signature", init_test_signature, clean_test_signature);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test extract_signature()", test_extract_signature) == NULL) {
        return NULL;
    }

    return pSuite;
}
