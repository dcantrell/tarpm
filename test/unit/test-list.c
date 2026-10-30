/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_list(void)
{
    return 0;
}

int
clean_test_list(void)
{
    return 0;
}

void
test_list_rpm_null_input(void)
{
    /* test that NULL input fails */
    TARPM_ASSERT_EQUAL(list_rpm(NULL), -1);

    return;
}

void
test_list_rpm_empty_string(void)
{
    /* test that empty string fails */
    TARPM_ASSERT_EQUAL(list_rpm(""), -1);

    return;
}

void
test_list_rpm_nonexistent_file(void)
{
    /* test that a nonexistent file fails */
    TARPM_ASSERT_EQUAL(list_rpm("/nonexistent/path/to/package.rpm"), -1);

    return;
}

void
test_list_rpm_invalid_file(void)
{
    /* test that an invalid (non-RPM) file fails */
    TARPM_ASSERT_EQUAL(list_rpm("/dev/null"), -1);

    return;
}

void
test_list_rpm_directory(void)
{
    /* test that a directory path fails */
    TARPM_ASSERT_EQUAL(list_rpm("/tmp"), -1);

    return;
}

void
test_list_rpm_special_characters(void)
{
    /* test that paths with special characters fail */
    TARPM_ASSERT_EQUAL(list_rpm("/path/with spaces/package.rpm"), -1);
    TARPM_ASSERT_EQUAL(list_rpm("/path/with\ttabs/package.rpm"), -1);
    TARPM_ASSERT_EQUAL(list_rpm("/path/with\nnewlines/package.rpm"), -1);

    return;
}

void
test_list_rpm_very_long_path(void)
{
    char long_path[4096];
    size_t i = 0;

    /* create a very long path */
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';

    /* add some path separators */
    for (i = 0; i < sizeof(long_path) - 10; i += 50) {
        long_path[i] = '/';
    }

    /* test that very long paths are handled gracefully */
    TARPM_ASSERT_EQUAL(list_rpm(long_path), -1);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("list", init_test_list, clean_test_list);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test list_rpm() null input", test_list_rpm_null_input) == NULL ||
        CU_add_test(pSuite, "test list_rpm() empty string", test_list_rpm_empty_string) == NULL ||
        CU_add_test(pSuite, "test list_rpm() nonexistent file", test_list_rpm_nonexistent_file) == NULL ||
        CU_add_test(pSuite, "test list_rpm() invalid file", test_list_rpm_invalid_file) == NULL ||
        CU_add_test(pSuite, "test list_rpm() directory", test_list_rpm_directory) == NULL ||
        CU_add_test(pSuite, "test list_rpm() special characters", test_list_rpm_special_characters) == NULL ||
        CU_add_test(pSuite, "test list_rpm() very long path", test_list_rpm_very_long_path) == NULL) {
        return NULL;
    }

    return pSuite;
}
