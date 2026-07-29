/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_strmode(void)
{
    return 0;
}

int
clean_test_strmode(void)
{
    return 0;
}

void
test_strmode(void)
{
    char buf[12];

    /* test regular file with 0644 permissions */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | 0644, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rw-r--r-- ") == 0);

    /* test directory with 0755 permissions */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFDIR | 0755, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "drwxr-xr-x ") == 0);

    /* test symbolic link with 0777 permissions */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFLNK | 0777, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "lrwxrwxrwx ") == 0);

    /* test character device */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFCHR | 0666, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "crw-rw-rw- ") == 0);

    /* test block device */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFBLK | 0660, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "brw-rw---- ") == 0);

    /* test socket */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFSOCK | 0755, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "srwxr-xr-x ") == 0);

#ifdef S_IFIFO
    /* test FIFO */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFIFO | 0644, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "prw-r--r-- ") == 0);
#endif

    /* test setuid bit */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | S_ISUID | 0755, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rwsr-xr-x ") == 0);

    /* test setuid without execute */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | S_ISUID | 0644, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rwSr--r-- ") == 0);

    /* test setgid bit */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | S_ISGID | 0755, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rwxr-sr-x ") == 0);

    /* test setgid without execute */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | S_ISGID | 0644, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rw-r-Sr-- ") == 0);

    /* test sticky bit */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFDIR | S_ISVTX | 0755, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "drwxr-xr-t ") == 0);

    /* test sticky without execute */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFDIR | S_ISVTX | 0644, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "drw-r--r-T ") == 0);

    /* test no permissions */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | 0000, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "---------- ") == 0);

    /* test all permissions */
    memset(buf, 0, sizeof(buf));
    strmode(S_IFREG | 0777, buf);
    TARPM_ASSERT_TRUE(strcmp(buf, "-rwxrwxrwx ") == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("strmode", init_test_strmode, clean_test_strmode);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test strmode()", test_strmode) == NULL) {
        return NULL;
    }

    return pSuite;
}
