/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_inodes(void)
{
    return 0;
}

int
clean_test_inodes(void)
{
    return 0;
}

void
test_lookup_inode(void)
{
    char *result = NULL;

    /* lookup with inode 0 should return NULL */
    result = lookup_inode(0);
    TARPM_ASSERT_TRUE(result == NULL);

    /* lookup non-existent inode should return NULL */
    result = lookup_inode(999999);
    TARPM_ASSERT_TRUE(result == NULL);

    return;
}

void
test_free_inodes(void)
{
    /* calling free_inodes() when empty should not crash */
    free_inodes();

    /* calling free_inodes() multiple times should not crash */
    free_inodes();
    free_inodes();

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("inodes", init_test_inodes, clean_test_inodes);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test lookup_inode()", test_lookup_inode) == NULL ||
        CU_add_test(pSuite, "test free_inodes()", test_free_inodes) == NULL) {
        return NULL;
    }

    return pSuite;
}
