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
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
    TARPM_ASSERT_TRUE(len == 1);

    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_TRUE(strcmp(entry->str, "foo") == 0);
    }

    list_free(list, free);

    /* delimiter same as string returns single entry list */
    list = strsplit(",", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
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
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
    TARPM_ASSERT_TRUE(len == 3);
    list_free(list, free);

    /* split empty string */
    list = strsplit("", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
    TARPM_ASSERT_TRUE(len == 1);
    list_free(list, free);

    /* split single element (no delimiter found) */
    list = strsplit("foobar", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
    TARPM_ASSERT_TRUE(len == 1);
    TAILQ_FOREACH(entry, list, items) {
        TARPM_ASSERT_TRUE(strcmp(entry->str, "foobar") == 0);
    }
    list_free(list, free);

    /* split with delimiter at start */
    list = strsplit(",foo,bar", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
    TARPM_ASSERT_TRUE(len == 3);
    list_free(list, free);

    /* split with delimiter at end */
    list = strsplit("foo,bar,", ",");
    TARPM_ASSERT_TRUE(list != NULL);
    len = 0;
    TAILQ_FOREACH(entry, list, items) {
        len++;
    }
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

void
test_escape_name(void)
{
    char *s = NULL;

    TARPM_ASSERT_TRUE(escape_name(NULL) == NULL);

    s = escape_name("");
    TARPM_ASSERT_TRUE(strcmp(s, "") == 0);
    free(s);

    /* printable names come back as they are, UTF-8 included */
    s = escape_name("/usr/bin/caf\xc3\xa9 ok");
    TARPM_ASSERT_TRUE(strcmp(s, "/usr/bin/caf\xc3\xa9 ok") == 0);
    free(s);

    /* control characters become octal escapes */
    s = escape_name("a\033[2Jb\nc\x7f");
    TARPM_ASSERT_TRUE(strcmp(s, "a\\033[2Jb\\012c\\177") == 0);
    free(s);

    /* a backslash is doubled so the escapes stay unambiguous */
    s = escape_name("a\\012");
    TARPM_ASSERT_TRUE(strcmp(s, "a\\\\012") == 0);
    free(s);

    return;
}

void
test_str_or_empty(void)
{
    const char *s = "abc";

    TARPM_ASSERT_STRING_EQUAL(str_or_empty(NULL), "");
    TARPM_ASSERT_TRUE(str_or_empty(s) == s);

    return;
}

/* Test human_size() picks the unit and rounds to one place */
void
test_human_size(void)
{
    char buf[HUMAN_SIZE_LEN];

    human_size(0, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "0 bytes");
    human_size(1, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "1 byte");
    human_size(1023, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "1023 bytes");
    human_size(1024, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "1.0 KiB");
    human_size(1536, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "1.5 KiB");
    human_size(5 * 1024 * 1024, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "5.0 MiB");
    human_size(UINT64_MAX, buf, sizeof(buf));
    TARPM_ASSERT_STRING_EQUAL(buf, "16777216.0 TiB");

    /* a buffer too small is cut short, not overrun */
    human_size(1024, buf, 4);
    TARPM_ASSERT_STRING_EQUAL(buf, "1.0");

    return;
}

/* Test verbose_stage() and verbose_detail() print only with -v */
void
test_verbose_lines(void)
{
    char *line = NULL;
    size_t len = 0;
    FILE *out = NULL;

    out = tmpfile();
    TARPM_ASSERT_PTR_NOT_NULL(out);
    verbose_out = out;

    /* without -v nothing is printed */
    verbose = false;
    verbose_stage("quiet %d", 1);
    verbose_detail("quiet %d", 2);
    TARPM_ASSERT_EQUAL(ftell(out), 0);

    /* stages get a prefix, details are indented, control characters escaped */
    verbose = true;
    verbose_stage("Reading %s", "header.json");
    verbose_detail("./%s", "bad\033name");

    rewind(out);
    TARPM_ASSERT_TRUE(getline(&line, &len, out) > 0);
    TARPM_ASSERT_STRING_EQUAL(line, ">>> Reading header.json\n");
    TARPM_ASSERT_TRUE(getline(&line, &len, out) > 0);
    TARPM_ASSERT_STRING_EQUAL(line, "  ./bad\\033name\n");
    TARPM_ASSERT_TRUE(getline(&line, &len, out) == -1);

    free(line);
    fclose(out);
    verbose = false;
    verbose_out = NULL;

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
    if (CU_add_test(pSuite, "test strappend()", test_strappend) == NULL ||
        CU_add_test(pSuite, "test strsplit()", test_strsplit) == NULL ||
        CU_add_test(pSuite, "test escape_name()", test_escape_name) == NULL ||
        CU_add_test(pSuite, "test str_or_empty()", test_str_or_empty) == NULL ||
        CU_add_test(pSuite, "test human_size()", test_human_size) == NULL ||
        CU_add_test(pSuite, "test verbose_stage() and verbose_detail()", test_verbose_lines) == NULL) {
        return NULL;
    }

    return pSuite;
}
