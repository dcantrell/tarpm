/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <string.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_deps(void)
{
    return 0;
}

int
clean_test_deps(void)
{
    return 0;
}

/* Test generate_dependencies with NULL inputs */
void
test_generate_dependencies_null(void)
{
    struct json_object *result = NULL;

    result = generate_dependencies(NULL, NULL);
    TARPM_ASSERT_PTR_NULL(result);

    return;
}

/* Test add_dependency_tags with NULL inputs */
void
test_add_dependency_tags_null(void)
{
    struct json_object *tags = NULL;

    /* NULL tags should not crash */
    add_dependency_tags(NULL, NULL);

    /* NULL dependencies should not crash */
    tags = json_object_new_array();
    add_dependency_tags(tags, NULL);
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);
    json_object_put(tags);

    /* Both NULL should not crash */
    add_dependency_tags(NULL, NULL);

    return;
}

/* Test add_dependency_tags with empty dependencies */
void
test_add_dependency_tags_empty(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();

    add_dependency_tags(tags, dependencies);

    /* Empty dependencies should not add any tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with provides dependency */
void
test_add_dependency_tags_provides(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *provides = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    const char *tag_name = NULL;
    size_t i = 0;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    provides = json_object_new_array();

    /* Create a single provides entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("test-package"));
    json_object_object_add(entry, "comparison", json_object_new_string(">="));
    json_object_object_add(entry, "version", json_object_new_string("1.0"));
    json_object_array_add(provides, entry);

    json_object_object_add(dependencies, "provides", provides);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags: PROVIDENAME, PROVIDEFLAGS, PROVIDEVERSION */
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

    /* Verify PROVIDENAME tag */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Providename");

    /* Verify PROVIDEFLAGS tag */
    tag_entry = json_object_array_get_idx(tags, 1);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Provideflags");

    /* Verify PROVIDEVERSION tag */
    tag_entry = json_object_array_get_idx(tags, 2);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Provideversion");

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with requires dependency */
void
test_add_dependency_tags_requires(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    const char *tag_name = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    /* Create a single requires entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("libc.so.6"));
    json_object_array_add(requires, entry);

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Verify REQUIRENAME tag */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Requirename");

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with multiple dependency types */
void
test_add_dependency_tags_multiple_types(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *provides = NULL;
    struct json_object *requires = NULL;
    struct json_object *conflicts = NULL;
    struct json_object *entry = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();

    /* Create provides */
    provides = json_object_new_array();
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("test-package"));
    json_object_array_add(provides, entry);
    json_object_object_add(dependencies, "provides", provides);

    /* Create requires */
    requires = json_object_new_array();
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("libc.so.6"));
    json_object_array_add(requires, entry);
    json_object_object_add(dependencies, "requires", requires);

    /* Create conflicts */
    conflicts = json_object_new_array();
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("old-package"));
    json_object_array_add(conflicts, entry);
    json_object_object_add(dependencies, "conflicts", conflicts);

    add_dependency_tags(tags, dependencies);

    /* Should have 9 tags (3 for each dependency type) */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 9);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with multiple entries in single dependency type */
void
test_add_dependency_tags_multiple_entries(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    /* Create first requires entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("libc.so.6"));
    json_object_array_add(requires, entry);

    /* Create second requires entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("libm.so.6"));
    json_object_array_add(requires, entry);

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Each tag's value array should have 2 entries */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    TARPM_ASSERT_EQUAL(json_object_array_length(tag_value), 2);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with all comparison operators */
void
test_add_dependency_tags_comparisons(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;
    struct json_object *entry = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    /* Test each comparison operator */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("pkg1"));
    json_object_object_add(entry, "comparison", json_object_new_string(">="));
    json_object_object_add(entry, "version", json_object_new_string("1.0"));
    json_object_array_add(requires, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("pkg2"));
    json_object_object_add(entry, "comparison", json_object_new_string("<="));
    json_object_object_add(entry, "version", json_object_new_string("2.0"));
    json_object_array_add(requires, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("pkg3"));
    json_object_object_add(entry, "comparison", json_object_new_string(">"));
    json_object_object_add(entry, "version", json_object_new_string("3.0"));
    json_object_array_add(requires, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("pkg4"));
    json_object_object_add(entry, "comparison", json_object_new_string("<"));
    json_object_object_add(entry, "version", json_object_new_string("4.0"));
    json_object_array_add(requires, entry);

    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("pkg5"));
    json_object_object_add(entry, "comparison", json_object_new_string("="));
    json_object_object_add(entry, "version", json_object_new_string("5.0"));
    json_object_array_add(requires, entry);

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with sense flags */
void
test_add_dependency_tags_sense_flags(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;
    struct json_object *entry = NULL;
    struct json_object *sense_flags = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    /* Create entry with sense flags */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("test-package"));
    sense_flags = json_object_new_array();
    json_object_array_add(sense_flags, json_object_new_string("pre"));
    json_object_array_add(sense_flags, json_object_new_string("rpmlib"));
    json_object_object_add(entry, "sense_flags", sense_flags);
    json_object_array_add(requires, entry);

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with empty array */
void
test_add_dependency_tags_empty_array(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Empty array should not add any tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with missing name field */
void
test_add_dependency_tags_missing_name(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *requires = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    struct json_object *name_value = NULL;
    const char *name_str = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    requires = json_object_new_array();

    /* Create entry without name */
    entry = json_object_new_object();
    json_object_object_add(entry, "comparison", json_object_new_string(">="));
    json_object_object_add(entry, "version", json_object_new_string("1.0"));
    json_object_array_add(requires, entry);

    json_object_object_add(dependencies, "requires", requires);

    add_dependency_tags(tags, dependencies);

    /* Should still create three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* REQUIRENAME should have empty string when name is missing */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "value", &tag_value);
    name_value = json_object_array_get_idx(tag_value, 0);
    name_str = json_object_get_string(name_value);
    TARPM_ASSERT_STRING_EQUAL(name_str, "");

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with obsoletes dependency */
void
test_add_dependency_tags_obsoletes(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *obsoletes = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    const char *tag_name = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    obsoletes = json_object_new_array();

    /* Create a single obsoletes entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("old-package"));
    json_object_object_add(entry, "comparison", json_object_new_string("<"));
    json_object_object_add(entry, "version", json_object_new_string("2.0"));
    json_object_array_add(obsoletes, entry);

    json_object_object_add(dependencies, "obsoletes", obsoletes);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Verify OBSOLETENAME tag */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Obsoletename");

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test add_dependency_tags with recommends dependency */
void
test_add_dependency_tags_recommends(void)
{
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *recommends = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag_entry = NULL;
    struct json_object *tag_value = NULL;
    const char *tag_name = NULL;

    tags = json_object_new_array();
    dependencies = json_object_new_object();
    recommends = json_object_new_array();

    /* Create a single recommends entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string("optional-package"));
    json_object_array_add(recommends, entry);

    json_object_object_add(dependencies, "recommends", recommends);

    add_dependency_tags(tags, dependencies);

    /* Should have three tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 3);

    /* Verify RECOMMENDNAME tag */
    tag_entry = json_object_array_get_idx(tags, 0);
    json_object_object_get_ex(tag_entry, "tag", &tag_value);
    tag_name = json_object_get_string(tag_value);
    TARPM_ASSERT_STRING_EQUAL(tag_name, "Recommendname");

    json_object_put(tags);
    json_object_put(dependencies);

    return;
}

/* Test is_dependency_tag with dependency tags */
void
test_is_dependency_tag_dependency_tags(void)
{
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_PROVIDENAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_PROVIDEFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_PROVIDEVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_REQUIRENAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_REQUIREFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_REQUIREVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_CONFLICTNAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_CONFLICTFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_CONFLICTVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_OBSOLETENAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_OBSOLETEFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_OBSOLETEVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_RECOMMENDNAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_RECOMMENDFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_RECOMMENDVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUGGESTNAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUGGESTFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUGGESTVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUPPLEMENTNAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUPPLEMENTFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_SUPPLEMENTVERSION));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_ENHANCENAME));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_ENHANCEFLAGS));
    TARPM_ASSERT_TRUE(is_dependency_tag(RPMTAG_ENHANCEVERSION));

    return;
}

/* Test is_dependency_tag with non-dependency tags */
void
test_is_dependency_tag_non_dependency_tags(void)
{
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_NAME));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_VERSION));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_RELEASE));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_ARCH));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_SUMMARY));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_DESCRIPTION));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_CHANGELOGTIME));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_CHANGELOGNAME));
    TARPM_ASSERT_FALSE(is_dependency_tag(RPMTAG_CHANGELOGTEXT));

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("deps", init_test_deps, clean_test_deps);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_dependencies() with NULL", test_generate_dependencies_null) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with NULL", test_add_dependency_tags_null) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with empty dependencies", test_add_dependency_tags_empty) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with provides", test_add_dependency_tags_provides) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with requires", test_add_dependency_tags_requires) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with multiple types", test_add_dependency_tags_multiple_types) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with multiple entries", test_add_dependency_tags_multiple_entries) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with comparisons", test_add_dependency_tags_comparisons) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with sense flags", test_add_dependency_tags_sense_flags) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with empty array", test_add_dependency_tags_empty_array) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with missing name", test_add_dependency_tags_missing_name) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with obsoletes", test_add_dependency_tags_obsoletes) == NULL ||
        CU_add_test(pSuite, "test add_dependency_tags() with recommends", test_add_dependency_tags_recommends) == NULL ||
        CU_add_test(pSuite, "test is_dependency_tag() with dependency tags", test_is_dependency_tag_dependency_tags) == NULL ||
        CU_add_test(pSuite, "test is_dependency_tag() with non-dependency tags", test_is_dependency_tag_non_dependency_tags) == NULL) {
        return NULL;
    }

    return pSuite;
}
