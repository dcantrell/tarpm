/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_write(void)
{
    return 0;
}

int
clean_test_write(void)
{
    return 0;
}

void
test_generate_json(void)
{
    TARPM_ASSERT_TRUE(generate_json(NULL, NULL) == NULL);

    return;
}

void
test_generate_json_entries(void)
{
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, true) == NULL);
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, false) == NULL);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("write", init_test_write, clean_test_write);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_json()", test_generate_json) == NULL ||
        CU_add_test(pSuite, "test generate_json_entries()", test_generate_json_entries) == NULL) {
        return NULL;
    }

    return pSuite;
}
