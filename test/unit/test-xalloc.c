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
    int i = 0;

    buf = xcalloc(47, 47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= (47 * 47));

    /* verify memory is zeroed */
    for (i = 0; i < 47 * 47; i++) {
        TARPM_ASSERT_TRUE(buf[i] == 0);
    }

    free(buf);

    /* test single element allocation */
    buf = xcalloc(1, 100);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 100);
    free(buf);

    /* test small allocation */
    buf = xcalloc(1, 1);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 1);
    free(buf);

    return;
}

void
test_xalloc(void)
{
    char *buf = NULL;
    char *buf2 = NULL;

    buf = xalloc(47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 47);
    free(buf);

    /* test small allocation */
    buf = xalloc(1);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 1);
    free(buf);

    /* test larger allocation */
    buf = xalloc(1024);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 1024);
    free(buf);

    /* test multiple allocations */
    buf = xalloc(100);
    buf2 = xalloc(200);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(buf2 != NULL);
    TARPM_ASSERT_TRUE(buf != buf2);
    free(buf);
    free(buf2);

    return;
}

void
test_xrealloc(void)
{
    char *buf = NULL;

    buf = xalloc(47);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 47);

    /* expand allocation */
    buf = xrealloc(buf, 147);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 147);

    /* expand again */
    buf = xrealloc(buf, 1024);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 1024);

    /* shrink allocation */
    buf = xrealloc(buf, 64);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 64);

    free(buf);

    /* test realloc of NULL pointer (should act like malloc) */
    buf = NULL;
    buf = xrealloc(buf, 100);
    TARPM_ASSERT_TRUE(buf != NULL);
    TARPM_ASSERT_TRUE(malloc_usable_size(buf) >= 100);
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
