/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <time.h>
#include <string.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_changelog(void)
{
    return 0;
}

int
clean_test_changelog(void)
{
    return 0;
}

/* Test generate_changelog with NULL inputs */
void
test_generate_changelog_null(void)
{
    struct json_object *result = NULL;

    result = generate_changelog(NULL, NULL);
    TARPM_ASSERT_PTR_NULL(result);

    return;
}

/* Test add_changelog_tags with NULL inputs */
void
test_add_changelog_tags_null(void)
{
    struct json_object *tags = NULL;

    /* NULL tags should not crash */
    add_changelog_tags(NULL, NULL);

    /* NULL changelog should not crash */
    tags = json_object_new_array();
    add_changelog_tags(tags, NULL);
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);
    json_object_put(tags);

    /* Both NULL should not crash */
    add_changelog_tags(NULL, NULL);

    return;
}

/* Test add_changelog_tags with empty changelog */
void
test_add_changelog_tags_empty(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    add_changelog_tags(tags, changelog);

    /* Empty changelog should not add any tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with invalid changelog type */
void
test_add_changelog_tags_invalid_type(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_string("not an array");

    add_changelog_tags(tags, changelog);

    /* Invalid type should not add any tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with a single changelog entry */
void
test_add_changelog_tags_single_entry(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    const char *tag_name = NULL;
    size_t i = 0;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create a single changelog entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));

    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- First change"));
    json_object_array_add(text, json_object_new_string("- Second change"));
    json_object_object_add(entry, "text", text);

    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should have three tags: CHANGELOGTIME, CHANGELOGNAME, CHANGELOGTEXT */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Verify each tag has correct structure */
    for (i = 0; i < json_object_array_length(tags); i++) {
        tag_entry = json_object_array_get_idx(tags, i);
        TARPM_ASSERT_PTR_NOT_NULL(tag_entry);

        /* Each tag should have "tag", "type", and "value" fields */
        TARPM_ASSERT_TRUE(json_object_object_get_ex(tag_entry, "tag", NULL));
        TARPM_ASSERT_TRUE(json_object_object_get_ex(tag_entry, "type", NULL));
        TARPM_ASSERT_TRUE(json_object_object_get_ex(tag_entry, "value", NULL));
    }

    /* Verify CHANGELOGTIME tag */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Changelogtime");

    /* Verify CHANGELOGNAME tag */
    tag_entry = json_object_array_get_idx(tags, 1);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Changelogname");

    /* Verify CHANGELOGTEXT tag */
    tag_entry = json_object_array_get_idx(tags, 2);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Changelogtext");

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with multiple changelog entries */
void
test_add_changelog_tags_multiple_entries(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    size_t i = 0;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create first changelog entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));
    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- First change"));
    json_object_object_add(entry, "text", text);
    json_object_array_add(changelog, entry);

    /* Create second changelog entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Thu Jul 30 2020"));
    json_object_object_add(entry, "name", json_object_new_string("Jane Smith <jane@example.com>"));
    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- Second change"));
    json_object_object_add(entry, "text", text);
    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Each tag's value array should have 2 entries */
    for (i = 0; i < 3; i++) {
        tag_entry = json_object_array_get_idx(tags, i);
        json_object_object_get_ex(tag_entry, "value", &tag_value);
        TARPM_ASSERT_EQUAL(json_object_array_length(tag_value), 2);
    }

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with extended timestamp format */
void
test_add_changelog_tags_extended_timestamp(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    struct json_object *time_value = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry with extended timestamp format */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Thu Oct 6 06:48:39 UTC 2016"));
    json_object_object_add(entry, "name", json_object_new_string("Test User <test@example.com>"));
    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- Test change"));
    json_object_object_add(entry, "text", text);
    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Get CHANGELOGTIME value */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    time_value = json_object_array_get_idx(tag_value, 0);

    /* Timestamp should be non-zero */
    TARPM_ASSERT_TRUE(json_object_get_int(time_value) > 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with multiline changelog text */
void
test_add_changelog_tags_multiline_text(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    struct json_object *text_value = NULL;
    const char *text_str = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry with multiple text lines */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));

    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("Line 1"));
    json_object_array_add(text, json_object_new_string("Line 2"));
    json_object_array_add(text, json_object_new_string("Line 3"));
    json_object_object_add(entry, "text", text);

    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Get CHANGELOGTEXT value */
    tag_entry = json_object_array_get_idx(tags, 2);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    text_value = json_object_array_get_idx(tag_value, 0);
    text_str = json_object_get_string(text_value);

    /* Text should be joined with newlines */
    TARPM_ASSERT_STRING_EQUAL(text_str, "Line 1\nLine 2\nLine 3");

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with empty text array */
void
test_add_changelog_tags_empty_text(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    struct json_object *text_value = NULL;
    const char *text_str = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry with empty text array */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));

    text = json_object_new_array();
    json_object_object_add(entry, "text", text);

    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Get CHANGELOGTEXT value */
    tag_entry = json_object_array_get_idx(tags, 2);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    text_value = json_object_array_get_idx(tag_value, 0);
    text_str = json_object_get_string(text_value);

    /* Text should be empty string */
    TARPM_ASSERT_STRING_EQUAL(text_str, "");

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with missing timestamp field */
void
test_add_changelog_tags_missing_timestamp(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry without timestamp */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));
    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- Change"));
    json_object_object_add(entry, "text", text);
    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should still create three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* CHANGELOGTIME should have no entry when timestamp is missing */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    TARPM_ASSERT_EQUAL(json_object_array_length(tag_value), 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with missing name field */
void
test_add_changelog_tags_missing_name(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *text = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry without name */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    text = json_object_new_array();
    json_object_array_add(text, json_object_new_string("- Change"));
    json_object_object_add(entry, "text", text);
    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should still create three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* CHANGELOGNAME should have no entry when name is missing */
    tag_entry = json_object_array_get_idx(tags, 1);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    TARPM_ASSERT_EQUAL(json_object_array_length(tag_value), 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

/* Test add_changelog_tags with missing text field */
void
test_add_changelog_tags_missing_text(void)
{
    struct json_object *tags = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;

    tags = json_object_new_array();
    changelog = json_object_new_array();

    /* Create changelog entry without text */
    entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_string("Wed Jul 29 2020"));
    json_object_object_add(entry, "name", json_object_new_string("John Doe <john@example.com>"));
    json_object_array_add(changelog, entry);

    add_changelog_tags(tags, changelog);

    /* Should still create three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* CHANGELOGTEXT should have no entry when text is missing */
    tag_entry = json_object_array_get_idx(tags, 2);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    TARPM_ASSERT_EQUAL(json_object_array_length(tag_value), 0);

    json_object_put(tags);
    json_object_put(changelog);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("changelog", init_test_changelog, clean_test_changelog);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_changelog() with NULL", test_generate_changelog_null) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with NULL", test_add_changelog_tags_null) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with empty changelog", test_add_changelog_tags_empty) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with invalid type", test_add_changelog_tags_invalid_type) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with single entry", test_add_changelog_tags_single_entry) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with multiple entries", test_add_changelog_tags_multiple_entries) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with extended timestamp", test_add_changelog_tags_extended_timestamp) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with multiline text", test_add_changelog_tags_multiline_text) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with empty text", test_add_changelog_tags_empty_text) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with missing timestamp", test_add_changelog_tags_missing_timestamp) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with missing name", test_add_changelog_tags_missing_name) == NULL ||
        CU_add_test(pSuite, "test add_changelog_tags() with missing text", test_add_changelog_tags_missing_text) == NULL) {
        return NULL;
    }

    return pSuite;
}
