/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_unpack(void)
{
    return 0;
}

int
clean_test_unpack(void)
{
    return 0;
}

void
test_unpack_archive(void)
{
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, true, true) == -1);
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, false, true) == -1);
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, true, false) == -1);
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, false, false) == -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("unpack", init_test_unpack, clean_test_unpack);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test unpack_archive()", test_unpack_archive) == NULL) {
        return NULL;
    }

    return pSuite;
}
