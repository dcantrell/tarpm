/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_create(void)
{
    return 0;
}

int
clean_test_create(void)
{
    return 0;
}

/*
 * Test create_rpm() with missing arguments.  Every argument is
 * required, so each of these calls must return without doing anything.
 */
void
test_create_rpm_null(void)
{
    create_rpm(NULL, NULL, NULL, NULL);
    create_rpm(NULL, "/tmp", "/tmp", NULL);
    create_rpm("test.rpm", NULL, "/tmp", NULL);
    create_rpm("test.rpm", "/tmp", NULL, NULL);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("create", init_test_create, clean_test_create);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test create_rpm() with NULL", test_create_rpm_null) == NULL) {
        return NULL;
    }

    return pSuite;
}
