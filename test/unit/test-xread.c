/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_xread(void)
{
    return 0;
}

int
clean_test_xread(void)
{
    return 0;
}

void
test_xread(void)
{
    int fd = -1;
    char buf[47];
    char tmpfile[] = "/tmp/tarpm-test-xread-XXXXXX";
    char *expected = "test content\n";

    /* basic read check */
    fd = open("/proc/uptime", O_RDONLY);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(xread(fd, &buf, 3));
    fd = close(fd);
    TARPM_ASSERT_FALSE(fd == -1);

    /* a NULL destination buffer should fail */
    TARPM_ASSERT_FALSE(xread(STDIN_FILENO, NULL, 47));

    /* invalid file descriptor should fail */
    TARPM_ASSERT_FALSE(xread(-1, &buf, 10));

    /* reading zero bytes should succeed */
    fd = open("/proc/uptime", O_RDONLY);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(xread(fd, &buf, 0));
    fd = close(fd);
    TARPM_ASSERT_FALSE(fd == -1);

    /* create a temporary test file of a known size */
    fd = mkstemp(tmpfile);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(write(fd, expected, strlen(expected)) == (ssize_t) strlen(expected));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* reading exactly what is available should succeed */
    fd = open(tmpfile, O_RDONLY);
    TARPM_ASSERT_FALSE(fd == -1);
    memset(buf, 0, sizeof(buf));
    TARPM_ASSERT_TRUE(xread(fd, &buf, strlen(expected)));
    TARPM_ASSERT_STRING_EQUAL(buf, expected);
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* reading more than is available should fail */
    fd = open(tmpfile, O_RDONLY);
    TARPM_ASSERT_FALSE(fd == -1);
    memset(buf, 0, sizeof(buf));
    TARPM_ASSERT_FALSE(xread(fd, &buf, sizeof(buf)));
    TARPM_ASSERT_TRUE(close(fd) == 0);

    /* clean up */
    TARPM_ASSERT_TRUE(unlink(tmpfile) == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("xread", init_test_xread, clean_test_xread);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test xread()", test_xread) == NULL) {
        return NULL;
    }

    return pSuite;
}
