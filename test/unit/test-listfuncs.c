/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <malloc.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_listfuncs(void)
{
    return 0;
}

int
clean_test_listfuncs(void)
{
    return 0;
}

void
test_list_add(void)
{
    str_list_t *list = NULL;
    str_entry_t *entry = NULL;
    int r = 0;

    list = list_add(list, "foo");
    TARPM_ASSERT_TRUE(list != NULL);

    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_TRUE(entry != NULL);
        TARPM_ASSERT_TRUE(entry->str != NULL);
        r = strcmp(entry->str, "foo");
        TARPM_ASSERT_TRUE(r == 0);
    }

    list_free(list, free);
    return;
}

void
test_list_free(void)
{
    str_list_t *list = NULL;

    list = list_add(list, "foo");
    TARPM_ASSERT_TRUE(list != NULL);
    list_free(list, free);

    return;
}

void
test_list_to_string(void)
{
    str_list_t *list = NULL;
    char *s = NULL;
    int r = 0;

    list = list_add(list, "foo");
    list = list_add(list, "bar");
    list = list_add(list, "baz");
    list = list_add(list, "qux");
    TARPM_ASSERT_TRUE(list != NULL);
    s = list_to_string(list, ",");
    TARPM_ASSERT_TRUE(s != NULL);
    r = strcmp(s, "foo,bar,baz,qux");
    TARPM_ASSERT_TRUE(r == 0);
    list_free(list, free);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("listfuncs", init_test_listfuncs, clean_test_listfuncs);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test list_add()", test_list_add) == NULL ||
        CU_add_test(pSuite, "test list_free()", test_list_free) == NULL ||
        CU_add_test(pSuite, "test list_to_string()", test_list_to_string) == NULL) {
        return NULL;
    }

    return pSuite;
}
