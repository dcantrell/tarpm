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

    /* test empty prefix - should match */
    TARPM_ASSERT_TRUE(strprefix("flargenblarfle", ""));

    /* test full string match */
    TARPM_ASSERT_TRUE(strprefix("flarg", "flarg"));

    /* test prefix longer than string */
    TARPM_ASSERT_FALSE(strprefix("abc", "abcdef"));

    /* test NULL cases */
    TARPM_ASSERT_FALSE(strprefix(NULL, "prefix"));
    TARPM_ASSERT_FALSE(strprefix("string", NULL));
    TARPM_ASSERT_FALSE(strprefix(NULL, NULL));

    /* test single character */
    TARPM_ASSERT_TRUE(strprefix("abc", "a"));
    TARPM_ASSERT_FALSE(strprefix("abc", "b"));

    return;
}

void
test_strsuffix(void)
{
    TARPM_ASSERT_TRUE(strsuffix("flargenblarfle", "blarfle"));
    TARPM_ASSERT_FALSE(strsuffix("flargenblarfle", "monkey"));

    /* test empty suffix - should match */
    TARPM_ASSERT_TRUE(strsuffix("flargenblarfle", ""));

    /* test full string match */
    TARPM_ASSERT_TRUE(strsuffix("blarfle", "blarfle"));

    /* test suffix longer than string */
    TARPM_ASSERT_FALSE(strsuffix("abc", "xyzabc"));

    /* test NULL cases */
    TARPM_ASSERT_FALSE(strsuffix(NULL, "suffix"));
    TARPM_ASSERT_FALSE(strsuffix("string", NULL));
    TARPM_ASSERT_FALSE(strsuffix(NULL, NULL));

    /* test single character */
    TARPM_ASSERT_TRUE(strsuffix("abc", "c"));
    TARPM_ASSERT_FALSE(strsuffix("abc", "b"));

    /* test case sensitivity */
    TARPM_ASSERT_FALSE(strsuffix("file.TXT", "txt"));
    TARPM_ASSERT_TRUE(strsuffix("file.txt", "txt"));

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

    /* test multiple appends */
    a = strdup("one");
    assert(a != NULL);
    a = strappend(a, " two", NULL);
    a = strappend(a, " three", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "one two three") == 0);
    free(a);

    /* test appending empty string */
    a = strdup("hello");
    assert(a != NULL);
    a = strappend(a, "", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "hello") == 0);
    free(a);

    /* test appending multiple parts in one call */
    a = NULL;
    a = strappend(a, "part1", " part2", " part3", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "part1 part2 part3") == 0);
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

    /* split empty string */
    list = strsplit("", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 1);
    list_free(list, free);

    /* split single element (no delimiter found) */
    list = strsplit("foobar", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 1);
    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_TRUE(strcmp(entry->str, "foobar") == 0);
    }
    list_free(list, free);

    /* split with delimiter at start */
    list = strsplit(",foo,bar", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 3);
    list_free(list, free);

    /* split with delimiter at end */
    list = strsplit("foo,bar,", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = list_len(list);
    TARPM_ASSERT_TRUE(len == 3);
    list_free(list, free);

    /* split with space delimiter */
    list = strsplit("one two three", " ");
    TARPM_ASSERT_TRUE(list != NULL);
    s = list_to_string(list, "|");
    TARPM_ASSERT_TRUE(strcmp(s, "one|two|three") == 0);
    free(s);
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
