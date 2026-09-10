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

void
test_uint32_list_add(void)
{
    uint32_list_t *list = NULL;
    uint32_entry_t *entry = NULL;
    uint32_t expected = 0;

    list = uint32_list_add(list, 47);
    TARPM_ASSERT_PTR_NOT_NULL(list);

    list = uint32_list_add(list, 0);
    list = uint32_list_add(list, 4294967295U);

    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_PTR_NOT_NULL(entry);

        if (expected == 0) {
            TARPM_ASSERT_EQUAL(entry->value, 47);
        } else if (expected == 1) {
            TARPM_ASSERT_EQUAL(entry->value, 0);
        } else {
            TARPM_ASSERT_EQUAL(entry->value, 4294967295U);
        }

        expected++;
    }

    TARPM_ASSERT_EQUAL(expected, 3);
    uint32_list_free(list);

    return;
}

void
test_uint32_list_free(void)
{
    uint32_list_t *list = NULL;

    /* a NULL list is safe to free */
    uint32_list_free(NULL);

    list = uint32_list_add(list, 47);
    TARPM_ASSERT_PTR_NOT_NULL(list);
    uint32_list_free(list);

    return;
}

void
test_str_list_len(void)
{
    str_list_t *list = NULL;

    /* a NULL list has no entries */
    TARPM_ASSERT_EQUAL(str_list_len(NULL), 0);

    list = list_add(list, "foo");
    TARPM_ASSERT_EQUAL(str_list_len(list), 1);

    list = list_add(list, "bar");
    list = list_add(list, "baz");
    TARPM_ASSERT_EQUAL(str_list_len(list), 3);

    list_free(list, free);

    return;
}

void
test_uint32_list_len(void)
{
    uint32_list_t *list = NULL;

    /* a NULL list has no entries */
    TARPM_ASSERT_EQUAL(uint32_list_len(NULL), 0);

    list = uint32_list_add(list, 1);
    TARPM_ASSERT_EQUAL(uint32_list_len(list), 1);

    list = uint32_list_add(list, 2);
    list = uint32_list_add(list, 3);
    TARPM_ASSERT_EQUAL(uint32_list_len(list), 3);

    uint32_list_free(list);

    return;
}

void
test_str_list_nth(void)
{
    str_list_t *list = NULL;

    /* a NULL list has nothing to return */
    TARPM_ASSERT_TRUE(str_list_nth(NULL, 0) == NULL);

    list = list_add(list, "foo");
    list = list_add(list, "bar");
    list = list_add(list, "baz");

    TARPM_ASSERT_STRING_EQUAL(str_list_nth(list, 0), "foo");
    TARPM_ASSERT_STRING_EQUAL(str_list_nth(list, 1), "bar");
    TARPM_ASSERT_STRING_EQUAL(str_list_nth(list, 2), "baz");

    /* an index past the end of the list returns nothing */
    TARPM_ASSERT_TRUE(str_list_nth(list, 3) == NULL);

    list_free(list, free);

    return;
}

void
test_uint32_list_nth(void)
{
    uint32_list_t *list = NULL;
    uint32_t value = 0;

    /* a NULL list and a NULL result have nothing to return */
    TARPM_ASSERT_FALSE(uint32_list_nth(NULL, 0, &value));

    list = uint32_list_add(list, 42);
    list = uint32_list_add(list, 47);
    list = uint32_list_add(list, 0);

    TARPM_ASSERT_FALSE(uint32_list_nth(list, 0, NULL));

    TARPM_ASSERT_TRUE(uint32_list_nth(list, 0, &value));
    TARPM_ASSERT_EQUAL(value, 42);

    TARPM_ASSERT_TRUE(uint32_list_nth(list, 1, &value));
    TARPM_ASSERT_EQUAL(value, 47);

    TARPM_ASSERT_TRUE(uint32_list_nth(list, 2, &value));
    TARPM_ASSERT_EQUAL(value, 0);

    /* an index past the end of the list returns nothing */
    TARPM_ASSERT_FALSE(uint32_list_nth(list, 3, &value));

    uint32_list_free(list);

    return;
}

void
test_first_str_and_next_str(void)
{
    str_list_t *list = NULL;
    str_entry_t *entry = NULL;

    /* a NULL list and a NULL entry have no members */
    TARPM_ASSERT_TRUE(first_str(NULL) == NULL);
    TARPM_ASSERT_TRUE(next_str(NULL) == NULL);

    list = list_add(list, "foo");
    list = list_add(list, "bar");

    entry = first_str(list);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_STRING_EQUAL(entry->str, "foo");

    entry = next_str(entry);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_STRING_EQUAL(entry->str, "bar");

    /* walking past the last entry gives nothing */
    entry = next_str(entry);
    TARPM_ASSERT_TRUE(entry == NULL);

    list_free(list, free);

    return;
}

void
test_first_uint32_and_next_uint32(void)
{
    uint32_list_t *list = NULL;
    uint32_entry_t *entry = NULL;

    /* a NULL list and a NULL entry have no members */
    TARPM_ASSERT_TRUE(first_uint32(NULL) == NULL);
    TARPM_ASSERT_TRUE(next_uint32(NULL) == NULL);

    list = uint32_list_add(list, 47);
    list = uint32_list_add(list, 74);

    entry = first_uint32(list);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_EQUAL(entry->value, 47);

    entry = next_uint32(entry);
    TARPM_ASSERT_PTR_NOT_NULL(entry);
    TARPM_ASSERT_EQUAL(entry->value, 74);

    /* walking past the last entry gives nothing */
    entry = next_uint32(entry);
    TARPM_ASSERT_TRUE(entry == NULL);

    uint32_list_free(list);

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
        CU_add_test(pSuite, "test list_to_string()", test_list_to_string) == NULL ||
        CU_add_test(pSuite, "test uint32_list_add()", test_uint32_list_add) == NULL ||
        CU_add_test(pSuite, "test uint32_list_free()", test_uint32_list_free) == NULL ||
        CU_add_test(pSuite, "test str_list_len()", test_str_list_len) == NULL ||
        CU_add_test(pSuite, "test uint32_list_len()", test_uint32_list_len) == NULL ||
        CU_add_test(pSuite, "test str_list_nth()", test_str_list_nth) == NULL ||
        CU_add_test(pSuite, "test uint32_list_nth()", test_uint32_list_nth) == NULL ||
        CU_add_test(pSuite, "test first_str() and next_str()", test_first_str_and_next_str) == NULL ||
        CU_add_test(pSuite, "test first_uint32() and next_uint32()", test_first_uint32_and_next_uint32) == NULL) {
        return NULL;
    }

    return pSuite;
}
