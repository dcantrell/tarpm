/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_reset(void)
{
    return 0;
}

int
clean_test_reset(void)
{
    return 0;
}

void
test_reset_librpm(void)
{
    reset_librpm();

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("reset", init_test_reset, clean_test_reset);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test reset_librpm()", test_reset_librpm) == NULL) {
        return NULL;
    }

    return pSuite;
}
