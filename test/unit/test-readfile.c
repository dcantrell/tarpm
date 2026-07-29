/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_readfile(void)
{
    return 0;
}

int
clean_test_readfile(void)
{
    return 0;
}

void
test_read_file_bytes(void)
{
    void *data = NULL;
    off_t len = 0;
    int fd = -1;
    char tmpfile[] = "/tmp/tarpm-test-XXXXXX";
    char tmpfile2[] = "/tmp/tarpm-test-XXXXXX";
    char *expected = "test content\n";

    /* test non-existent file */
    data = read_file_bytes("/nonexistent/file/path", &len);
    TARPM_ASSERT_TRUE(data == NULL);

    /* create a temporary test file */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(write(fd, expected, strlen(expected)) == (ssize_t)strlen(expected));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* test reading a real file */
    data = read_file_bytes(tmpfile, &len);
    TARPM_ASSERT_TRUE(data != NULL);
    TARPM_ASSERT_TRUE(len == (off_t)strlen(expected));
    TARPM_ASSERT_TRUE(memcmp(data, expected, len) == 0);
    free(data);

    /* clean up */
    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    /* test empty file - should return NULL */
    fd = mkstemp(tmpfile2);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(close(fd) == 0);
    data = read_file_bytes(tmpfile2, &len);
    TARPM_ASSERT_TRUE(data == NULL);
    TARPM_ASSERT_TRUE(unlink(tmpfile2) == 0);

    return;
}

void
test_read_file(void)
{
    char *data = NULL;
    int fd = -1;
    char tmpfile[] = "/tmp/tarpm-test-XXXXXX";
    char tmpfile2[] = "/tmp/tarpm-test-XXXXXX";
    char *expected = "test content\n";

    /* test non-existent file */
    data = read_file("/nonexistent/file/path");
    TARPM_ASSERT_TRUE(data == NULL);

    /* create a temporary test file */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(write(fd, expected, strlen(expected)) == (ssize_t)strlen(expected));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* test reading a real file */
    data = read_file(tmpfile);
    TARPM_ASSERT_TRUE(data != NULL);
    TARPM_ASSERT_TRUE(strcmp(data, expected) == 0);
    free(data);

    /* clean up */
    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    /* test empty file - should return NULL */
    fd = mkstemp(tmpfile2);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(close(fd) == 0);
    data = read_file(tmpfile2);
    TARPM_ASSERT_TRUE(data == NULL);
    TARPM_ASSERT_TRUE(unlink(tmpfile2) == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("readfile", init_test_readfile, clean_test_readfile);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test read_file_bytes()", test_read_file_bytes) == NULL ||
        CU_add_test(pSuite, "test read_file()", test_read_file) == NULL) {
        return NULL;
    }

    return pSuite;
}
