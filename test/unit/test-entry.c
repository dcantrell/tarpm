/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <rpm/rpmtag.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_entry(void)
{
    return 0;
}

int
clean_test_entry(void)
{
    return 0;
}

void
test_is_file_tag(void)
{
    /* RPMTAG_SPEC should be a file tag */
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_DESCRIPTION));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREIN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTIN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREUN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTUN));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PRETRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTTRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_PREUNTRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_POSTUNTRANS));
    TARPM_ASSERT_TRUE(is_file_tag(RPMTAG_SPEC));

    /* other common tags should not be file tags */
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_NAME));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_VERSION));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_RELEASE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_SUMMARY));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_BUILDTIME));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_BUILDHOST));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_SIZE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_LICENSE));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_PACKAGER));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_GROUP));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_URL));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_OS));
    TARPM_ASSERT_FALSE(is_file_tag(RPMTAG_ARCH));

    return;
}

void
test_get_tag_filename(void)
{
    char *filename = NULL;

    /* test RPMTAG_SPEC with default ending */
    filename = get_tag_filename(RPMTAG_SPEC, NULL);
    TARPM_ASSERT_TRUE(filename != NULL);
    TARPM_ASSERT_TRUE(strcmp(filename, "spec") == 0);
    free(filename);

    /* test RPMTAG_SPEC with .txt ending */
    filename = get_tag_filename(RPMTAG_SPEC, "txt");
    TARPM_ASSERT_TRUE(filename != NULL);
    TARPM_ASSERT_TRUE(strcmp(filename, "spec.txt") == 0);
    free(filename);

    /* test non-file tag should return NULL */
    filename = get_tag_filename(RPMTAG_NAME, NULL);
    TARPM_ASSERT_TRUE(filename == NULL);

    filename = get_tag_filename(RPMTAG_VERSION, "txt");
    TARPM_ASSERT_TRUE(filename == NULL);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("entry", init_test_entry, clean_test_entry);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test is_file_tag()", test_is_file_tag) == NULL ||
        CU_add_test(pSuite, "test get_tag_filename()", test_get_tag_filename) == NULL) {
        return NULL;
    }

    return pSuite;
}
