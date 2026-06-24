/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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

    /* basic read check */
    fd = open("/proc/uptime", O_RDONLY);
    TARPM_ASSERT_FALSE(fd == -1);
    TARPM_ASSERT_TRUE(xread(fd, &buf, 3));
    fd = close(fd);
    TARPM_ASSERT_FALSE(fd == -1);

    /* a NULL destination buffer should fail */
    TARPM_ASSERT_FALSE(xread(STDIN_FILENO, NULL, 47));

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
