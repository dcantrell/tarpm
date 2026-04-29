/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_read(void)
{
    return 0;
}

int
clean_test_read(void)
{
    return 0;
}

void
test_compute_sigvalues(void)
{
    TARPM_ASSERT_TRUE(compute_sigvalues(NULL, true) == NULL);
    TARPM_ASSERT_TRUE(compute_sigvalues(NULL, false) == NULL);

    return;
}

void
test_read_header_signature(void)
{
    TARPM_ASSERT_TRUE(read_header_signature(0) == NULL);
    TARPM_ASSERT_TRUE(read_header_signature(-47) == NULL);

    return;
}

void
test_read_header_entries(void)
{
    TARPM_ASSERT_TRUE(read_header_entries(0, NULL, 47) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(-47, NULL, 47) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(0, NULL, 0) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(-47, NULL, -47) == NULL);

    return;
}

void
test_read_header_trailer(void)
{
    TARPM_ASSERT_TRUE(read_header_trailer(NULL, NULL) == NULL);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("read", init_test_read, clean_test_read);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test compute_sigvalues()", test_compute_sigvalues) == NULL ||
        CU_add_test(pSuite, "test read_header_signature()", test_read_header_signature) == NULL ||
        CU_add_test(pSuite, "test read_header_entries()", test_read_header_entries) == NULL ||
        CU_add_test(pSuite, "test read_header_trailer()", test_read_header_trailer) == NULL) {
        return NULL;
    }

    return pSuite;
}
