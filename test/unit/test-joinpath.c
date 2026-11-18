/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
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
     * extra leading slashes and middle slashes reduced to one.  Extra
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
    struct json_object *seen = NULL;

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

    /* the seen form gives the same answers and keys only the directories it passed */
    seen = json_object_new_object();
    TARPM_ASSERT_TRUE(path_beneath_seen(tmpdir, "/usr/bin/missing", false, seen));
    TARPM_ASSERT_TRUE(json_object_object_get_ex(seen, "usr", NULL));
    TARPM_ASSERT_TRUE(json_object_object_get_ex(seen, "usr/bin", NULL));
    TARPM_ASSERT_FALSE(json_object_object_get_ex(seen, "usr/bin/missing", NULL));
    TARPM_ASSERT_TRUE(path_beneath_seen(tmpdir, "usr/bin/link", true, seen));
    TARPM_ASSERT_FALSE(path_beneath_seen(tmpdir, "usr/bin/link", false, seen));
    TARPM_ASSERT_FALSE(path_beneath_seen(tmpdir, "lib/link", true, seen));
    TARPM_ASSERT_FALSE(json_object_object_get_ex(seen, "lib", NULL));
    TARPM_ASSERT_FALSE(path_beneath_seen(tmpdir, "usr/../etc/passwd", false, seen));
    TARPM_ASSERT_EQUAL(json_object_object_length(seen), 2);
    json_object_put(seen);

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

/* Assert opener fails on path below basefd with the given errno */
static void
assert_open_at_fails(int (*opener)(const int, const char *), const int basefd, const char *path, const int expected)
{
    errno = 0;
    TARPM_ASSERT_EQUAL(opener(basefd, path), -1);
    TARPM_ASSERT_EQUAL(errno, expected);
    return;
}

/*
 * Run the descriptor cases against open_beneath_at() and its fallback.
 * One base descriptor serves every open and stays open after.
 */
static void
check_opener_at(int (*opener)(const int, const char *))
{
    int i = 0;
    int fd = -1;
    int basefd = -1;
    char tmpdir[] = "/tmp/tarpm-test-joinpath-XXXXXX";
    char *path = NULL;
    char buf[8];
    FILE *fp = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(tmpdir) != NULL);

    /* usr/bin/file is a file, lib a symlink to usr/bin, link a symlink to file */
    xasprintf(&path, "%s/usr", tmpdir);
    TARPM_ASSERT_TRUE(mkdir(path, 0755) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin", tmpdir);
    TARPM_ASSERT_TRUE(mkdir(path, 0755) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin/file", tmpdir);
    fp = fopen(path, "w");
    TARPM_ASSERT_PTR_NOT_NULL(fp);
    fputs("data", fp);
    fclose(fp);
    free(path);
    xasprintf(&path, "%s/lib", tmpdir);
    TARPM_ASSERT_TRUE(symlink("usr/bin", path) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin/link", tmpdir);
    TARPM_ASSERT_TRUE(symlink("file", path) == 0);
    free(path);

    basefd = open_beneath_base(tmpdir);
    TARPM_ASSERT_TRUE(basefd >= 0);

    /* the don't crash cases */
    assert_open_at_fails(opener, basefd, NULL, EINVAL);
    assert_open_at_fails(opener, -1, "usr/bin/file", EBADF);

    /* the same base opens the file again and again */
    for (i = 0; i < 3; i++) {
        fd = opener(basefd, "/usr/bin/file");
        TARPM_ASSERT_TRUE(fd >= 0);
        memset(buf, 0, sizeof(buf));
        TARPM_ASSERT_EQUAL(read(fd, buf, sizeof(buf)), 4);
        TARPM_ASSERT_STRING_EQUAL(buf, "data");
        close(fd);
    }

    /* the leading slash is optional */
    fd = opener(basefd, "usr/bin/file");
    TARPM_ASSERT_TRUE(fd >= 0);
    close(fd);

    /* no symlink is followed, on the way or at the end */
    assert_open_at_fails(opener, basefd, "lib/file", ELOOP);
    assert_open_at_fails(opener, basefd, "/usr/bin/link", ELOOP);

    /* names that leave the base or are not plain */
    assert_open_at_fails(opener, basefd, "", EINVAL);
    assert_open_at_fails(opener, basefd, "/", EINVAL);
    assert_open_at_fails(opener, basefd, "../etc/passwd", EINVAL);
    assert_open_at_fails(opener, basefd, "usr/../usr/bin/file", EINVAL);
    assert_open_at_fails(opener, basefd, "usr/./bin/file", EINVAL);
    assert_open_at_fails(opener, basefd, "usr//bin/file", EINVAL);

    /* a missing file is just missing */
    assert_open_at_fails(opener, basefd, "usr/bin/missing", ENOENT);

    /* none of that closed the base */
    TARPM_ASSERT_TRUE(fcntl(basefd, F_GETFD) != -1);
    close(basefd);

    /* clean up */
    xasprintf(&path, "%s/usr/bin/link", tmpdir);
    TARPM_ASSERT_TRUE(unlink(path) == 0);
    free(path);
    xasprintf(&path, "%s/usr/bin/file", tmpdir);
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

void
test_open_beneath_at(void)
{
    check_opener_at(open_beneath_at);
    return;
}

void
test_open_beneath_walk_at(void)
{
    check_opener_at(open_beneath_walk_at);
    return;
}

/* Test open_beneath_base() only opens a directory */
void
test_open_beneath_base(void)
{
    int fd = -1;

    errno = 0;
    TARPM_ASSERT_EQUAL(open_beneath_base(NULL), -1);
    TARPM_ASSERT_EQUAL(errno, EINVAL);

    errno = 0;
    TARPM_ASSERT_EQUAL(open_beneath_base("/nonexistent-tarpm-dir"), -1);
    TARPM_ASSERT_EQUAL(errno, ENOENT);

    errno = 0;
    TARPM_ASSERT_EQUAL(open_beneath_base("/dev/null"), -1);
    TARPM_ASSERT_EQUAL(errno, ENOTDIR);

    fd = open_beneath_base("/");
    TARPM_ASSERT_TRUE(fd >= 0);
    close(fd);

    return;
}

/* Test open_beneath_readable_at() opens a file with no owner read bit */
void
test_open_beneath_readable_at(void)
{
    int fd = -1;
    int basefd = -1;
    char tmpdir[] = "/tmp/tarpm-test-joinpath-XXXXXX";
    char *path = NULL;
    char buf[8];
    FILE *fp = NULL;
    struct stat sb;

    TARPM_ASSERT_TRUE(mkdtemp(tmpdir) != NULL);

    xasprintf(&path, "%s/shadow", tmpdir);
    fp = fopen(path, "w");
    TARPM_ASSERT_PTR_NOT_NULL(fp);
    fputs("data", fp);
    fclose(fp);
    TARPM_ASSERT_TRUE(chmod(path, 04111) == 0);

    basefd = open_beneath_base(tmpdir);
    TARPM_ASSERT_TRUE(basefd >= 0);

    /* the file reads and the mode is the one we started with */
    fd = open_beneath_readable_at(basefd, "shadow");
    TARPM_ASSERT_TRUE(fd >= 0);
    memset(buf, 0, sizeof(buf));
    TARPM_ASSERT_EQUAL(read(fd, buf, sizeof(buf)), 4);
    TARPM_ASSERT_STRING_EQUAL(buf, "data");
    close(fd);
    TARPM_ASSERT_TRUE(stat(path, &sb) == 0);
    TARPM_ASSERT_EQUAL(sb.st_mode & 07777, 04111);

    /* a missing file is just missing */
    errno = 0;
    TARPM_ASSERT_EQUAL(open_beneath_readable_at(basefd, "missing"), -1);
    TARPM_ASSERT_EQUAL(errno, ENOENT);

    close(basefd);
    TARPM_ASSERT_TRUE(unlink(path) == 0);
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
        CU_add_test(pSuite, "test path_beneath()", test_path_beneath) == NULL ||
        CU_add_test(pSuite, "test open_beneath_at()", test_open_beneath_at) == NULL ||
        CU_add_test(pSuite, "test open_beneath_walk_at()", test_open_beneath_walk_at) == NULL ||
        CU_add_test(pSuite, "test open_beneath_base()", test_open_beneath_base) == NULL ||
        CU_add_test(pSuite, "test open_beneath_readable_at()", test_open_beneath_readable_at) == NULL) {
        return NULL;
    }

    return pSuite;
}
