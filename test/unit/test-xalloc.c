/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <malloc.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_xalloc(void)
{
    return 0;
}

int
clean_test_xalloc(void)
{
    return 0;
}

void
test_xcalloc(void)
{
    char *buf = NULL;

    buf = xcalloc(47, 47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= (47 * 47));
    free(buf);

    return;
}

void
test_xalloc(void)
{
    char *buf = NULL;

    buf = xalloc(47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 47);
    free(buf);

    return;
}

void
test_xrealloc(void)
{
    char *buf = NULL;

    buf = xalloc(47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 47);

    buf = xrealloc(buf, 147);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 147);
    free(buf);

    return;
}

#ifdef _HAVE_REALLOCARRAY
void
test_xreallocarray(void)
{
    char *buf = NULL;

    buf = xcalloc(47, 47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= (47 * 47));

    buf = xreallocarray(buf, 147, 147);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= (147 * 147));
    free(buf);

    return;
}
#endif

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("xalloc", init_test_xalloc, clean_test_xalloc);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test xcalloc()", test_xcalloc) == NULL ||
        CU_add_test(pSuite, "test xalloc()", test_xalloc) == NULL ||
#ifdef _HAVE_REALLOCARRAY
        CU_add_test(pSuite, "test xreallocarray()", test_xreallocarray) == NULL ||
#endif
        CU_add_test(pSuite, "test xrealloc()", test_xrealloc) == NULL) {
        return NULL;
    }

    return pSuite;
}
