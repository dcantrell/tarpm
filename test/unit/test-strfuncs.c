/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_strfuncs(void)
{
    return 0;
}

int
clean_test_strfuncs(void)
{
    return 0;
}

void
test_strprefix(void)
{
    TARPM_ASSERT_TRUE(strprefix("flargenblarfle", "flarg"));
    TARPM_ASSERT_FALSE(strprefix("flargenblarfle", "monkey"));
    return;
}

void
test_strsuffix(void)
{
    TARPM_ASSERT_TRUE(strsuffix("flargenblarfle", "blarfle"));
    TARPM_ASSERT_FALSE(strsuffix("flargenblarfle", "monkey"));
    return;
}

void
test_strappend(void)
{
    char *a = NULL;

    /* check the ridiculous case */
    a = strappend(a, NULL);
    TARPM_ASSERT_TRUE(a == NULL);

    /* check the complicated-way-to-strdup case */
    a = strappend(a, "here is a string", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "here is a string") == 0);
    free(a);

    /* check that desired functionality works */
    a = strdup("This is a prefix");
    assert(a != NULL);
    a = strappend(a, " with a suffix.", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "This is a prefix with a suffix.") == 0);
    free(a);

    return;
}

void
test_strsplit(void)
{
    str_list_t *list = NULL;
    str_entry_t *entry = NULL;
    char *s = NULL;
    int len = 0;

    /* NULL input returns NULL */
    list = strsplit(NULL, ",");
    TARPM_ASSERT_TRUE(list == NULL);

    /* NULL delimiter returns single entry list */
    list = strsplit("foo", NULL);
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 1);

    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_TRUE(strcmp(entry->str, "foo") == 0);
    }

    list_free(list, free);

    /* delimiter same as string returns single entry list */
    list = strsplit(",", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 1);
    list_free(list, free);

    /* basic split on comma */
    list = strsplit("foo,bar,baz", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    s = list_to_string(list, "|");
    TARPM_ASSERT_TRUE(strcmp(s, "foo|bar|baz") == 0);
    free(s);
    list_free(list, free);

    /* split on multiple character delimiter */
    list = strsplit("foo::bar::baz", "::");
    TARPM_ASSERT_TRUE(list != NULL);
    s = list_to_string(list, ",");
    TARPM_ASSERT_TRUE(strcmp(s, "foo,,bar,,baz") == 0);
    free(s);
    list_free(list, free);

    /* split with empty tokens */
    list = strsplit("foo,,bar", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 3);
    list_free(list, free);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("strfuncs", init_test_strfuncs, clean_test_strfuncs);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test strprefix()", test_strprefix) == NULL ||
        CU_add_test(pSuite, "test strsuffix()", test_strsuffix) == NULL ||
        CU_add_test(pSuite, "test strappend()", test_strappend) == NULL ||
        CU_add_test(pSuite, "test strsplit()", test_strsplit) == NULL) {
        return NULL;
    }

    return pSuite;
}
