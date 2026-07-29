/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_json(void)
{
    return 0;
}

int
clean_test_json(void)
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
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, NULL, true) == NULL);
    TARPM_ASSERT_TRUE(generate_json_entries(NULL, NULL, NULL, NULL, false) == NULL);

    return;
}

void
test_write_json_file(void)
{
    TARPM_ASSERT_TRUE(write_json_file(NULL, NULL, NULL) == -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("json", init_test_json, clean_test_json);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_json()", test_generate_json) == NULL ||
        CU_add_test(pSuite, "test generate_json_entries()", test_generate_json_entries) == NULL ||
        CU_add_test(pSuite, "test write_json_file()", test_write_json_file) == NULL) {
        return NULL;
    }

    return pSuite;
}
