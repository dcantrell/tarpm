/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <unistd.h>
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
    char *cwd = NULL;
    char *expected = NULL;

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

    /* more complex path traversals */
    a = abspath("/usr/./local/./bin");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/bin") == 0);
    free(a);

    a = abspath("/usr/local/../share");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/share") == 0);
    free(a);

    a = abspath("/usr/./local/../share/./man");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/share/man") == 0);
    free(a);

    /* multiple consecutive slashes */
    a = abspath("/usr//local///bin");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/bin") == 0);
    free(a);

    /* trailing slashes are removed */
    a = abspath("/usr/local/bin/");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/bin") == 0);
    free(a);

    a = abspath("/usr/local/bin///");
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/bin") == 0);
    free(a);

    /*
     * a path without a leading slash is taken from the current
     * directory, wherever the test happens to be running
     */
    cwd = getcwd(NULL, 0);
    TARPM_ASSERT_PTR_NOT_NULL(cwd);
    xasprintf(&expected, "%s/usr/local/bin", cwd);

    a = abspath("usr/local/bin");
    TARPM_ASSERT_STRING_EQUAL(a, expected);
    free(a);

    a = abspath("./usr/local/bin");
    TARPM_ASSERT_STRING_EQUAL(a, expected);
    free(a);

    /* and it is simplified the same way an absolute path is */
    a = abspath("usr//local/./share/../bin/");
    TARPM_ASSERT_STRING_EQUAL(a, expected);
    free(a);

    free(expected);

    /* the current directory itself */
    a = abspath(".");
    TARPM_ASSERT_STRING_EQUAL(a, cwd);
    free(a);

    free(cwd);

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
