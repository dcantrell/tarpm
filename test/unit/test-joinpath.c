/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_joinpath(void)
{
    return 0;
}

int
clean_test_joinpath(void)
{
    return 0;
}

void
test_joinpath(void)
{
    char *a = NULL;

    /* the don't crash case */
    a = joinpath(NULL, NULL);
    TARPM_ASSERT_TRUE(a == NULL);

    /* simple path joining part I */
    a = joinpath("/usr", "bin", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin") == 0);
    free(a);

    /* simple path joining part II */
    a = joinpath("/", "proc", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/proc") == 0);
    free(a);

    /*
     * make sure a leading slash is added and any middle slashes
     */
    a = joinpath("usr", "share", "man", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/share/man") == 0);
    free(a);

    /*
     * extra leading slashes and middle slashes reduced to one; extra
     * trailing slashes reduced to one
     */
    a = joinpath("//////////usr/////////", "/////bin//////", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin/") == 0);
    free(a);

    /* if a trailing slash is present, keep it */
    a = joinpath("/usr", "/", "/bin/", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin/") == 0);
    free(a);

    /* but do not add a trailing slash if not specified */
    a = joinpath("/usr", "/", "/bin", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin") == 0);
    free(a);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("joinpath", init_test_joinpath, clean_test_joinpath);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test joinpath()", test_joinpath) == NULL) {
        return NULL;
    }

    return pSuite;
}
