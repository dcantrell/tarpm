/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <string.h>
#include <unistd.h>
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

/* Feed a lead through a pipe to read_lead() */
static struct json_object *
read_lead_bytes(const struct rpmlead *lead)
{
    int fds[2];
    struct json_object *r = NULL;

    if (pipe(fds) == -1) {
        return NULL;
    }

    if (write(fds[1], lead, RPMLEAD_SIZE) == RPMLEAD_SIZE) {
        r = read_lead(fds[0]);
    }

    close(fds[0]);
    close(fds[1]);
    return r;
}

void
test_read_lead(void)
{
    struct rpmlead lead;
    struct json_object *out = NULL;
    struct json_object *value = NULL;

    TARPM_ASSERT_TRUE(read_lead(-47) == NULL);

    /* a good lead gives every magic byte two digits */
    memset(&lead, 0, sizeof(lead));
    lead.magic[0] = RPMLEAD_MAGIC0;
    lead.magic[1] = RPMLEAD_MAGIC1;
    lead.magic[2] = RPMLEAD_MAGIC2;
    lead.magic[3] = RPMLEAD_MAGIC3;
    lead.major = 3;
    strcpy(lead.name, "testpkg-1.0-1");

    out = read_lead_bytes(&lead);
    TARPM_ASSERT_TRUE(out != NULL);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(out, RPM_LEAD_MAGIC, &value));
    TARPM_ASSERT_TRUE(strcmp(json_object_get_string(value), "0xEDABEEDB") == 0);
    json_object_put(out);

    /* a lead with the wrong magic is turned away */
    lead.magic[3] = 0x0B;
    TARPM_ASSERT_TRUE(read_lead_bytes(&lead) == NULL);

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
