/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <string.h>
#include <arpa/inet.h>
#include <json.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_lead(void)
{
    return 0;
}

int
clean_test_lead(void)
{
    return 0;
}

void
test_read_lead(void)
{
    TARPM_ASSERT_TRUE(read_lead(-47) == NULL);

    return;
}

void
test_create_lead(void)
{
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct rpmlead *lead = NULL;

    /* NULL input returns NULL */
    lead = create_lead(NULL);
    TARPM_ASSERT_TRUE(lead == NULL);

    /* create a minimal header JSON object */
    header = json_object_new_object();
    tags = json_object_new_array();

    /* add Name tag */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);

    /* add Version tag */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Version"));
    json_object_object_add(entry, "value", json_object_new_string("1.0"));
    json_object_array_add(tags, entry);

    /* add Release tag */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Release"));
    json_object_object_add(entry, "value", json_object_new_string("1"));
    json_object_array_add(tags, entry);

    json_object_object_add(header, "tags", tags);

    /* test creating lead from valid header - binary package */
    lead = create_lead(header);
    TARPM_ASSERT_TRUE(lead != NULL);
    TARPM_ASSERT_TRUE(lead->major == 3);
    TARPM_ASSERT_TRUE(lead->minor == 0);
    TARPM_ASSERT_TRUE(ntohs(lead->type) == 0);  /* binary package */
    TARPM_ASSERT_TRUE(strcmp(lead->name, "testpkg-1.0-1") == 0);
    free(lead);

    /* test with epoch */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Epoch"));
    json_object_object_add(entry, "value", json_object_new_string("2"));
    json_object_array_add(tags, entry);

    lead = create_lead(header);
    TARPM_ASSERT_TRUE(lead != NULL);
    TARPM_ASSERT_TRUE(strcmp(lead->name, "testpkg-2:1.0-1") == 0);
    free(lead);

    /* test source package */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Sourcepackage"));
    json_object_object_add(entry, "value", json_object_new_int(1));
    json_object_array_add(tags, entry);

    lead = create_lead(header);
    TARPM_ASSERT_TRUE(lead != NULL);
    TARPM_ASSERT_TRUE(ntohs(lead->type) == 1);  /* source package */
    free(lead);

    json_object_put(header);

    /* test header missing tags */
    header = json_object_new_object();
    lead = create_lead(header);
    TARPM_ASSERT_TRUE(lead == NULL);
    json_object_put(header);

    /* test header with missing required tags */
    header = json_object_new_object();
    tags = json_object_new_array();

    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string("Name"));
    json_object_object_add(entry, "value", json_object_new_string("testpkg"));
    json_object_array_add(tags, entry);

    /* missing Version and Release */
    json_object_object_add(header, "tags", tags);

    lead = create_lead(header);
    TARPM_ASSERT_TRUE(lead == NULL);
    json_object_put(header);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("lead", init_test_lead, clean_test_lead);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test read_lead()", test_read_lead) == NULL ||
        CU_add_test(pSuite, "test create_lead()", test_create_lead) == NULL) {
        return NULL;
    }

    return pSuite;
}
