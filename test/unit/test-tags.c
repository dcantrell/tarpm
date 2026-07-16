/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <CUnit/Basic.h>
#include <rpm/rpmtag.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_tags(void)
{
    return 0;
}

int
clean_test_tags(void)
{
    return 0;
}

void
test_strtagtype(void)
{
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_NULL_TYPE), "(null)") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_CHAR_TYPE), "char") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT8_TYPE), "int8") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT16_TYPE), "int16") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT32_TYPE), "int32") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_INT64_TYPE), "int64") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_STRING_TYPE), "string") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_BIN_TYPE), "binary blob") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_STRING_ARRAY_TYPE), "string array") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(RPM_I18NSTRING_TYPE), "i18n string") == 0);
    TARPM_ASSERT_TRUE(strcmp(strtagtype(47), "(unknown)") == 0);

    return;
}

void
test_sig_tag_name(void)
{
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(HEADER_SIGNATURES), "Headersignatures") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(HEADER_IMMUTABLE), "Headerimmutable") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SIZE), "Size") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LEMD5_1), "Lemd5_1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PGP), "Pgp") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LEMD5_2), "Lemd5_2") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_MD5), "Md5") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_GPG), "Gpg") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PGP5), "Pgp5") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_PAYLOADSIZE), "Payloadsize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_RESERVEDSPACE), "Reservedspace") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_BADSHA1_1), "Badsha1_1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_BADSHA1_2), "Badsha1_2") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_DSA), "Dsa") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_RSA), "Rsa") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SHA1), "Sha1") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LONGSIZE), "Longsize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_LONGARCHIVESIZE), "Longarchivesize") == 0);
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_SHA256), "Sha256") == 0);
#ifdef RPMSIGTAG_FILESIGNATURES
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURES), "Filesignatures") == 0);
#endif
#ifdef RPMSIGTAG_FILESIGNATURELENGTH
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_FILESIGNATURELENGTH), "Filesignaturelength") == 0);
#endif
#ifdef RPMSIGTAG_VERITYSIGNATURES
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATURES), "Veritysignatures") == 0);
#endif
#ifdef RPMSIGTAG_VERITYSIGNATUREALGO
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(RPMSIGTAG_VERITYSIGNATUREALGO), "Veritysignaturealgo") == 0);
#endif
    TARPM_ASSERT_TRUE(strcmp(sig_tag_name(0), "(unknown)") == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("tags", init_test_tags, clean_test_tags);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test strtagtype()", test_strtagtype) == NULL ||
        CU_add_test(pSuite, "test sig_tag_name()", test_sig_tag_name) == NULL) {
        return NULL;
    }

    return pSuite;
}
