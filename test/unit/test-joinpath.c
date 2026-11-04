/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <sys/stat.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_joinpath(void)
{
    return 0;
}

int
clean_test_joinpath(void)
{
    return 0;
}

void
test_joinpath(void)
{
    char *a = NULL;

    /* the don't crash case */
    a = joinpath(NULL, NULL);
    TARPM_ASSERT_TRUE(a == NULL);

    /* simple path joining part I */
    a = joinpath("/usr", "bin", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin") == 0);
    free(a);

    /* simple path joining part II */
    a = joinpath("/", "proc", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/proc") == 0);
    free(a);

    /*
     * make sure a leading slash is added and any middle slashes
     */
    a = joinpath("usr", "share", "man", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/share/man") == 0);
    free(a);

    /*
     * extra leading slashes and middle slashes reduced to one; extra
     * trailing slashes reduced to one
     */
    a = joinpath("//////////usr/////////", "/////bin//////", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin/") == 0);
    free(a);

    /* if a trailing slash is present, keep it */
    a = joinpath("/usr", "/", "/bin/", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin/") == 0);
    free(a);

    /* but do not add a trailing slash if not specified */
    a = joinpath("/usr", "/", "/bin", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin") == 0);
    free(a);

    /* single path component */
    a = joinpath("usr", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr") == 0);
    free(a);

    /* joining with empty string components */
    a = joinpath("/usr", "", "bin", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/bin") == 0);
    free(a);

    /* multiple path components */
    a = joinpath("/usr", "local", "share", "doc", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/usr/local/share/doc") == 0);
    free(a);

    /* joining absolute path with relative */
    a = joinpath("/home", "user", "documents", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/home/user/documents") == 0);
    free(a);

    /* all slashes */
    a = joinpath("/", "/", "/", NULL);
    TARPM_ASSERT_TRUE(strcmp(a, "/") == 0);
    free(a);

    return;
}

void
test_path_beneath(void)
{
    char tmpdir[] = "/tmp/tarpm-test-joinpath-XXXXXX";
    char *path = NULL;

    /* the don't crash case */
    TARPM_ASSERT_FALSE(path_beneath(NULL, "usr", false));
    TARPM_ASSERT_FALSE(path_beneath("/", NULL, false));

    TARPM_ASSERT_TRUE(mkdtemp(tmpdir) != NULL);

    /* usr/bin is a directory, lib is a symlink to it, file a symlink to a file */
    xasprintf(&path, "%s/usr", tmpdir);
    TARPM_ASSERT_TRUE(mkdir(path, 0755) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin", tmpdir);
    TARPM_ASSERT_TRUE(mkdir(path, 0755) == 0);
    free(path);
    xasprintf(&path, "%s/lib", tmpdir);
    TARPM_ASSERT_TRUE(symlink("usr/bin", path) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin/link", tmpdir);
    TARPM_ASSERT_TRUE(symlink("/etc/passwd", path) == 0);
    free(path);

    /* names below the base, there or not, with or without a leading slash */
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "usr/bin", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "/usr/bin", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "/usr/bin/missing", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "/no/such/dir", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "file..name", false));

    /* names that leave the base or are not plain */
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "/", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "..", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "/../etc/passwd", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "usr/../../etc/passwd", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "no/such/../dir", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "usr/./bin", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "usr//bin", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "usr/bin/", false));

    /* a symlinked directory on the way is never allowed */
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "lib/link", false));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "lib/link", true));

    /* a symlink at the end only when the caller allows it */
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "lib", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "lib", true));
    TARPM_ASSERT_FALSE(path_beneath(tmpdir, "usr/bin/link", false));
    TARPM_ASSERT_TRUE(path_beneath(tmpdir, "usr/bin/link", true));

    /* clean up */
    xasprintf(&path, "%s/usr/bin/link", tmpdir);
    TARPM_ASSERT_TRUE(unlink(path) == 0);
    free(path);
    xasprintf(&path, "%s/lib", tmpdir);
    TARPM_ASSERT_TRUE(unlink(path) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin", tmpdir);
    TARPM_ASSERT_TRUE(rmdir(path) == 0);
    free(path);
    xasprintf(&path, "%s/usr", tmpdir);
    TARPM_ASSERT_TRUE(rmdir(path) == 0);
    free(path);
    TARPM_ASSERT_TRUE(rmdir(tmpdir) == 0);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("joinpath", init_test_joinpath, clean_test_joinpath);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test joinpath()", test_joinpath) == NULL ||
        CU_add_test(pSuite, "test path_beneath()", test_path_beneath) == NULL) {
        return NULL;
    }

    return pSuite;
}
