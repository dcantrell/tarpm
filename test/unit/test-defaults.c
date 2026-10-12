/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <CUnit/Basic.h>
#include <json.h>
#include <rpm/rpmtag.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_defaults(void)
{
    return 0;
}

int
clean_test_defaults(void)
{
    return 0;
}

/* Add one string tag to a tags array */
static void
add_tag(struct json_object *tags, const rpmTagVal tag, const char *value)
{
    struct json_object *entry = NULL;

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(tag)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
    json_object_array_add(tags, entry);

    return;
}

/* Build a header carrying only the three tags a user has to supply */
static struct json_object *
make_minimal_header(struct json_object **tags)
{
    struct json_object *header = NULL;

    header = json_object_new_object();
    *tags = json_object_new_array();

    add_tag(*tags, RPMTAG_NAME, "hello");
    add_tag(*tags, RPMTAG_VERSION, "1.0");
    add_tag(*tags, RPMTAG_RELEASE, "1");

    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, *tags);

    return header;
}

/* Return the value of a tag in the header, NULL if it is not there */
static const char *
tag_value(struct json_object *header, const rpmTagVal tag)
{
    struct json_object *tags = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        return NULL;
    }

    return get_tag_value(tags, rpmTagGetName(tag));
}

/* Return the provides array of the header, NULL if it is not there */
static struct json_object *
header_provides(struct json_object *header)
{
    struct json_object *dependencies = NULL;
    struct json_object *provides = NULL;

    if (!json_object_object_get_ex(header, RPM_DEPENDENCIES_DESC, &dependencies)) {
        return NULL;
    }

    if (!json_object_object_get_ex(dependencies, RPM_DEPENDENCY_PROVIDES_KEY, &provides)) {
        return NULL;
    }

    return provides;
}

/* Test apply_header_defaults() fills in every tag a v4 package needs */
void
test_apply_header_defaults_v4(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    header = make_minimal_header(&tags);
    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_OS), RPM_DEFAULT_OS);
    TARPM_ASSERT_PTR_NOT_NULL((void *) tag_value(header, RPMTAG_ARCH));
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADFORMAT), RPM_DEFAULT_PAYLOADFORMAT);
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADCOMPRESSOR), RPM_DEFAULT_PAYLOADCOMPRESSOR);
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADFLAGS), RPM_DEFAULT_PAYLOADFLAGS);
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_SOURCERPM), RPM_DEFAULT_SOURCERPM);
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_FILEDIGESTALGO), "sha256");
    TARPM_ASSERT_PTR_NOT_NULL((void *) tag_value(header, RPMTAG_BUILDTIME));
    TARPM_ASSERT_PTR_NOT_NULL((void *) tag_value(header, RPMTAG_BUILDHOST));
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_SUMMARY), "hello");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_DESCRIPTION), "hello");

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() starts a v6 package out on zstd */
void
test_apply_header_defaults_v6(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    header = make_minimal_header(&tags);
    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V6), 0);

    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADCOMPRESSOR), RPMFORMAT_V6_COMPRESSOR);
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADFLAGS), RPMFORMAT_V6_COMPRESSOR_LEVEL);

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() never overwrites a tag the header carries */
void
test_apply_header_defaults_never_overwrites(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    header = make_minimal_header(&tags);
    add_tag(tags, RPMTAG_OS, "darwin");
    add_tag(tags, RPMTAG_ARCH, "sparc");
    add_tag(tags, RPMTAG_PAYLOADFORMAT, "clydesdale");
    add_tag(tags, RPMTAG_PAYLOADCOMPRESSOR, "xz");
    add_tag(tags, RPMTAG_PAYLOADFLAGS, "2");
    add_tag(tags, RPMTAG_SOURCERPM, "hello-1.0-1.src.rpm");
    add_tag(tags, RPMTAG_FILEDIGESTALGO, "md5");
    add_tag(tags, RPMTAG_BUILDTIME, "2020-01-01T00:00:00Z");
    add_tag(tags, RPMTAG_BUILDHOST, "builder.example.com");
    add_tag(tags, RPMTAG_SUMMARY, "a greeting");
    add_tag(tags, RPMTAG_DESCRIPTION, "a longer greeting");

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_OS), "darwin");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_ARCH), "sparc");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADFORMAT), "clydesdale");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADCOMPRESSOR), "xz");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_PAYLOADFLAGS), "2");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_SOURCERPM), "hello-1.0-1.src.rpm");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_FILEDIGESTALGO), "md5");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_BUILDTIME), "2020-01-01T00:00:00Z");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_BUILDHOST), "builder.example.com");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_SUMMARY), "a greeting");
    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_DESCRIPTION), "a longer greeting");

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() counts a file backed tag as present */
void
test_apply_header_defaults_tag_in_file(void)
{
    size_t i = 0;
    size_t found = 0;
    const char *name = NULL;
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    header = make_minimal_header(&tags);

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_DESCRIPTION)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_I18NSTRING_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_FILE_DESC, json_object_new_string("description.txt"));
    json_object_array_add(tags, entry);

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    /* there must still be exactly one Description tag */
    name = rpmTagGetName(RPMTAG_DESCRIPTION);

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &value) && !strcmp(name, json_object_get_string(value))) {
            found++;
        }
    }

    TARPM_ASSERT_EQUAL(found, 1);

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() leaves the digest algorithm alone for a tree that names files */
void
test_apply_header_defaults_keeps_digest_algo(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;

    header = make_minimal_header(&tags);

    files = json_object_new_array();
    file = json_object_new_object();
    json_object_object_add(file, RPM_FILE_PATH_DESC, json_object_new_string("/usr/bin/hello"));
    json_object_array_add(files, file);
    json_object_object_add(header, RPM_FILES_DESC, files);

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    TARPM_ASSERT_PTR_NULL((void *) tag_value(header, RPMTAG_FILEDIGESTALGO));

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() on a source package */
void
test_apply_header_defaults_source_package(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    header = make_minimal_header(&tags);
    add_tag(tags, RPMTAG_SOURCEPACKAGE, "1");

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    TARPM_ASSERT_STRING_EQUAL(tag_value(header, RPMTAG_ARCH), SRPM_ARCH_NAME);
    TARPM_ASSERT_PTR_NULL((void *) tag_value(header, RPMTAG_SOURCERPM));

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() adds the provide the package carries for itself */
void
test_apply_header_defaults_self_provide(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *provides = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    header = make_minimal_header(&tags);
    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    provides = header_provides(header);
    TARPM_ASSERT_PTR_NOT_NULL(provides);
    TARPM_ASSERT_EQUAL(json_object_array_length(provides), 1);

    entry = json_object_array_get_idx(provides, 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_DEPENDENCY_NAME_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "hello");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_DEPENDENCY_COMPARISON_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), COMPARISON_EQ);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_DEPENDENCY_VERSION_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "1.0-1");

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() puts an epoch above zero in the self provide */
void
test_apply_header_defaults_self_provide_epoch(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    header = make_minimal_header(&tags);
    add_tag(tags, RPMTAG_EPOCH, "2");

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);

    entry = json_object_array_get_idx(header_provides(header), 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_DEPENDENCY_VERSION_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "2:1.0-1");

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() keeps a self provide the header already has */
void
test_apply_header_defaults_keeps_self_provide(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *provides = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    header = make_minimal_header(&tags);

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_DEPENDENCY_NAME_DESC, json_object_new_string("hello"));
    json_object_object_add(entry, RPM_DEPENDENCY_VERSION_DESC, json_object_new_string("0.9-1"));

    provides = json_object_new_array();
    json_object_array_add(provides, entry);

    dependencies = json_object_new_object();
    json_object_object_add(dependencies, RPM_DEPENDENCY_PROVIDES_KEY, provides);
    json_object_object_add(header, RPM_DEPENDENCIES_DESC, dependencies);

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    TARPM_ASSERT_EQUAL(json_object_array_length(provides), 1);

    entry = json_object_array_get_idx(provides, 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(entry, RPM_DEPENDENCY_VERSION_DESC, &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "0.9-1");

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() run twice changes nothing the second time */
void
test_apply_header_defaults_twice(void)
{
    size_t ntags = 0;
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    header = make_minimal_header(&tags);

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    ntags = json_object_array_length(tags);

    TARPM_ASSERT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), ntags);
    TARPM_ASSERT_EQUAL(json_object_array_length(header_provides(header)), 1);

    json_object_put(header);

    return;
}

/* Test apply_header_defaults() with NULL and bad inputs */
void
test_apply_header_defaults_bad_input(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;

    TARPM_ASSERT_NOT_EQUAL(apply_header_defaults(NULL, RPM_FORMAT_V4), 0);

    header = json_object_new_object();
    TARPM_ASSERT_NOT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    json_object_put(header);

    header = json_object_new_object();
    tags = json_object_new_array();
    add_tag(tags, RPMTAG_NAME, "hello");
    add_tag(tags, RPMTAG_VERSION, "1.0");
    json_object_object_add(header, RPM_ENTRY_TAGS_DESC, tags);
    TARPM_ASSERT_NOT_EQUAL(apply_header_defaults(header, RPM_FORMAT_V4), 0);
    json_object_put(header);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("defaults", init_test_defaults, clean_test_defaults);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test apply_header_defaults() v4", test_apply_header_defaults_v4) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() v6", test_apply_header_defaults_v6) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() never overwrites", test_apply_header_defaults_never_overwrites) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() with a tag in a file", test_apply_header_defaults_tag_in_file) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() keeps the digest algorithm", test_apply_header_defaults_keeps_digest_algo) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() on a source package", test_apply_header_defaults_source_package) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() self provide", test_apply_header_defaults_self_provide) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() self provide with an epoch", test_apply_header_defaults_self_provide_epoch) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() keeps the self provide", test_apply_header_defaults_keeps_self_provide) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() run twice", test_apply_header_defaults_twice) == NULL ||
        CU_add_test(pSuite, "test apply_header_defaults() bad input", test_apply_header_defaults_bad_input) == NULL) {
        return NULL;
    }

    return pSuite;
}
