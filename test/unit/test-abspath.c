/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_abspath(void)
{
    return 0;
}

int
clean_test_abspath(void)
{
    return 0;
}

void
test_abspath(void)
{
    char *a = NULL;

    /* the don't crash case */
    a = abspath(NULL);
    TARPM_ASSERT_TRUE(a == NULL);

    /* no simplification */
    a = abspath("/usr/local/bin");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/bin") == 0);
    free(a);

    a = abspath("/");
    TARPM_ASSERT_TRUE(strcmp(a, "/") == 0);
    free(a);

    /* simplification */
    a = abspath("////////////////////usr/lib");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/lib") == 0);
    free(a);

    a = abspath("/usr/bin/../../bin");
    TARPM_ASSERT_TRUE(strcmp(a, "/bin") == 0);
    free(a);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("abspath", init_test_abspath, clean_test_abspath);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test abspath()", test_abspath) == NULL) {
        return NULL;
    }

    return pSuite;
}
