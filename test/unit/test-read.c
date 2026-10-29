/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <arpa/inet.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_read(void)
{
    return 0;
}

int
clean_test_read(void)
{
    return 0;
}

void
test_mkhdrinfo(void)
{
    TARPM_ASSERT_TRUE(mkhdrinfo(NULL, true) == NULL);
    TARPM_ASSERT_TRUE(mkhdrinfo(NULL, false) == NULL);

    return;
}

void
test_read_header_signature(void)
{
    TARPM_ASSERT_TRUE(read_header_signature(-47) == NULL);

    return;
}

void
test_read_header_entries(void)
{
    TARPM_ASSERT_TRUE(read_header_entries(0, NULL, 47) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(-47, NULL, 47) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(0, NULL, 0) == NULL);
    TARPM_ASSERT_TRUE(read_header_entries(-47, NULL, -47) == NULL);

    return;
}

void
test_read_header_trailer(void)
{
    TARPM_ASSERT_TRUE(read_header_trailer(NULL, NULL, NULL) == NULL);

    return;
}

/* Check one index entry against an 8 byte data area */
static bool
check_entry(const uint32_t type, const int32_t offset, const uint32_t count)
{
    struct rpmhdr hdr;
    struct rpmhdrentry entry;
    const uint8_t data[8] = { 'a', 'b', '\0', 'c', 'd', 'e', 'f', 'g' };

    hdr.nentries = 1;
    hdr.nbytes = sizeof(data);

    entry.tag = htonl(RPMTAG_NAME);
    entry.type = htonl(type);
    entry.offset = htonl((uint32_t) offset);
    entry.count = htonl(count);

    return valid_header_entries(&hdr, &entry, data);
}

void
test_valid_header_entries(void)
{
    struct rpmhdr hdr;

    hdr.nentries = 0;
    hdr.nbytes = 0;

    TARPM_ASSERT_FALSE(valid_header_entries(NULL, NULL, NULL));
    TARPM_ASSERT_TRUE(valid_header_entries(&hdr, (struct rpmhdrentry *) &hdr, (uint8_t *) &hdr));

    /* numbers inside and past the end of the data */
    TARPM_ASSERT_TRUE(check_entry(RPM_INT32_TYPE, 4, 1));
    TARPM_ASSERT_TRUE(check_entry(RPM_INT64_TYPE, 0, 1));
    TARPM_ASSERT_TRUE(check_entry(RPM_BIN_TYPE, 0, 8));
    TARPM_ASSERT_FALSE(check_entry(RPM_INT32_TYPE, 6, 1));
    TARPM_ASSERT_FALSE(check_entry(RPM_INT16_TYPE, 0, 5));
    TARPM_ASSERT_FALSE(check_entry(RPM_BIN_TYPE, 1, 8));
    TARPM_ASSERT_FALSE(check_entry(RPM_INT8_TYPE, 8, 1));

    /* a count that wraps a 32-bit size */
    TARPM_ASSERT_FALSE(check_entry(RPM_INT32_TYPE, 0, 0x40000001));

    /* negative offsets and a zero count */
    TARPM_ASSERT_FALSE(check_entry(RPM_INT8_TYPE, -1, 1));
    TARPM_ASSERT_FALSE(check_entry(RPM_INT8_TYPE, -16, 1));
    TARPM_ASSERT_FALSE(check_entry(RPM_INT32_TYPE, 0, 0));

    /* strings need a NUL before the end of the data */
    TARPM_ASSERT_TRUE(check_entry(RPM_STRING_TYPE, 0, 1));
    TARPM_ASSERT_TRUE(check_entry(RPM_I18NSTRING_TYPE, 1, 1));
    TARPM_ASSERT_FALSE(check_entry(RPM_STRING_TYPE, 3, 1));
    TARPM_ASSERT_FALSE(check_entry(RPM_STRING_ARRAY_TYPE, 0, 2));
    TARPM_ASSERT_FALSE(check_entry(RPM_STRING_TYPE, 8, 1));

    /* nothing reads the data of these, but the offset still counts */
    TARPM_ASSERT_TRUE(check_entry(RPM_NULL_TYPE, 1000, 0));
    TARPM_ASSERT_TRUE(check_entry(99, 4, 1000));
    TARPM_ASSERT_FALSE(check_entry(99, 1000, 1));

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("read", init_test_read, clean_test_read);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test mkhdrinfo()", test_mkhdrinfo) == NULL ||
        CU_add_test(pSuite, "test read_header_signature()", test_read_header_signature) == NULL ||
        CU_add_test(pSuite, "test read_header_entries()", test_read_header_entries) == NULL ||
        CU_add_test(pSuite, "test read_header_trailer()", test_read_header_trailer) == NULL ||
        CU_add_test(pSuite, "test valid_header_entries()", test_valid_header_entries) == NULL) {
        return NULL;
    }

    return pSuite;
}
