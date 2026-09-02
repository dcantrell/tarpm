/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <sys/stat.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_mkdirp(void)
{
    return 0;
}

int
clean_test_mkdirp(void)
{
    return 0;
}

void
test_mkdirp(void)
{
    char tmpdir1[] = "/tmp/tarpm-test-mkdirp-XXXXXX";
    char tmpdir2[] = "/tmp/tarpm-test-mkdirp-XXXXXX";
    char *nested = NULL;
    struct stat sb;
    mode_t mode = 0755;

    /* NULL path should fail */
    TARPM_ASSERT_TRUE(mkdirp(NULL, 0) == -1);

    /* create a simple directory */
    TARPM_ASSERT_TRUE(mkdtemp(tmpdir1) != NULL);
    TARPM_ASSERT_TRUE(mkdirp(tmpdir1, mode) == 0);
    TARPM_ASSERT_TRUE(stat(tmpdir1, &sb) == 0);
    TARPM_ASSERT_TRUE(S_ISDIR(sb.st_mode));
    TARPM_ASSERT_TRUE(rmdir(tmpdir1) == 0);

    /* create nested directories */
    TARPM_ASSERT_TRUE(mkdtemp(tmpdir2) != NULL);
    xasprintf(&nested, "%s/a/b/c/d", tmpdir2);
    TARPM_ASSERT_TRUE(mkdirp(nested, mode) == 0);
    TARPM_ASSERT_TRUE(stat(nested, &sb) == 0);
    TARPM_ASSERT_TRUE(S_ISDIR(sb.st_mode));

    /* calling mkdirp on existing directory should succeed */
    TARPM_ASSERT_TRUE(mkdirp(nested, mode) == 0);

    /* clean up nested directories */
    free(nested);
    xasprintf(&nested, "%s/a/b/c/d", tmpdir2);
    TARPM_ASSERT_TRUE(rmdir(nested) == 0);
    free(nested);
    xasprintf(&nested, "%s/a/b/c", tmpdir2);
    TARPM_ASSERT_TRUE(rmdir(nested) == 0);
    free(nested);
    xasprintf(&nested, "%s/a/b", tmpdir2);
    TARPM_ASSERT_TRUE(rmdir(nested) == 0);
    free(nested);
    xasprintf(&nested, "%s/a", tmpdir2);
    TARPM_ASSERT_TRUE(rmdir(nested) == 0);
    free(nested);
    TARPM_ASSERT_TRUE(rmdir(tmpdir2) == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("mkdirp", init_test_mkdirp, clean_test_mkdirp);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test mkdirp()", test_mkdirp) == NULL) {
        return NULL;
    }

    return pSuite;
}
