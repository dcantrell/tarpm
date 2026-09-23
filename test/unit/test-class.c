/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

/* where the files the file_class() tests look at live */
static char tmpdir[] = "/tmp/tarpm-test-class-XXXXXX";

int
init_test_class(void)
{
    return 0;
}

int
clean_test_class(void)
{
    close_magic();
    rmdir(tmpdir);
    return 0;
}

void
test_wanted_class(void)
{
    /* nothing to look at */
    TARPM_ASSERT_FALSE(wanted_class(NULL));

    /* no class at all is not a class rpm keeps */
    TARPM_ASSERT_FALSE(wanted_class(""));

    /* the token is the whole string */
    TARPM_ASSERT_TRUE(wanted_class("directory"));
    TARPM_ASSERT_TRUE(wanted_class("empty"));

    /* the token sits at the front */
    TARPM_ASSERT_TRUE(wanted_class("ELF 64-bit LSB pie executable, x86-64"));
    TARPM_ASSERT_TRUE(wanted_class("ELF 32-bit LSB shared object, Intel 80386"));
    TARPM_ASSERT_TRUE(wanted_class("Zip archive data, at least v2.0 to extract"));

    /* the token sits in the middle or at the end */
    TARPM_ASSERT_TRUE(wanted_class("ASCII text"));
    TARPM_ASSERT_TRUE(wanted_class("POSIX shell script, ASCII text executable"));
    TARPM_ASSERT_TRUE(wanted_class("PNG image data, 48 x 48"));
    TARPM_ASSERT_TRUE(wanted_class("HTML document, ASCII text"));
    TARPM_ASSERT_TRUE(wanted_class("Perl5 module source text"));
    TARPM_ASSERT_TRUE(wanted_class("pkgconfig file"));
    TARPM_ASSERT_TRUE(wanted_class("libtool library file"));

    /* the fixed names for the odd file types hold no token */
    TARPM_ASSERT_FALSE(wanted_class("character special"));
    TARPM_ASSERT_FALSE(wanted_class("block special"));
    TARPM_ASSERT_FALSE(wanted_class("fifo (named pipe)"));
    TARPM_ASSERT_FALSE(wanted_class("socket"));

    /* neither do these */
    TARPM_ASSERT_FALSE(wanted_class("data"));
    TARPM_ASSERT_FALSE(wanted_class("C Code"));
    TARPM_ASSERT_FALSE(wanted_class("C Header"));
    TARPM_ASSERT_FALSE(wanted_class("Apple binary property list"));

    /* the match is case sensitive */
    TARPM_ASSERT_FALSE(wanted_class("ASCII TEXT"));
    TARPM_ASSERT_FALSE(wanted_class("Directory"));

    /* the leading space in " text" has to be there */
    TARPM_ASSERT_FALSE(wanted_class("text"));

    return;
}

void
test_skipped_class(void)
{
    /* nothing to look at */
    TARPM_ASSERT_STRING_EQUAL(skipped_class(NULL), NULL);

    /* every ending we answer ourselves */
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/perl5/Foo.pm"), "Perl5 module source text");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/src/hello.c"), "C Code");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/include/stdio.h"), "C Header");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/lib64/libfoo.la"), "libtool library file");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/lib64/pkgconfig/foo.pc"), "pkgconfig file");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/doc/foo/index.html"), "HTML document");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/icons/foo.png"), "PNG image data");
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/icons/foo.svg"), "SVG Scalable Vector Graphics image");

    /* the ending may be the whole path */
    TARPM_ASSERT_STRING_EQUAL(skipped_class(".c"), "C Code");

    /* endings we have nothing to say about */
    TARPM_ASSERT_STRING_EQUAL(skipped_class(""), NULL);
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/bin/ls"), NULL);
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/lib64/libfoo.so.1"), NULL);
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/doc/foo/index.htm"), NULL);

    /* the ending has to be at the end */
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/foo.c/bar"), NULL);

    /* and the dot has to be there */
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/bin/c"), NULL);
    TARPM_ASSERT_STRING_EQUAL(skipped_class("/usr/share/misc/catalog.pm.gz"), NULL);

    return;
}

/* Build a stat that only carries a file type, which is all we read */
static void
set_mode(struct stat *sb, mode_t mode)
{
    memset(sb, 0, sizeof(*sb));
    sb->st_mode = mode;
    return;
}

/* Write a file in the scratch directory and give back its path */
static char *
make_file(const char *name, const char *contents)
{
    char *path = NULL;
    FILE *fp = NULL;

    xasprintf(&path, "%s/%s", tmpdir, name);
    fp = fopen(path, "w");
    TARPM_ASSERT_PTR_NOT_NULL(fp);

    if (fp == NULL) {
        return path;
    }

    if (contents != NULL) {
        fprintf(fp, "%s", contents);
    }

    fclose(fp);
    return path;
}

/* Take a scratch file away again and free the path */
static void
drop_file(char *path)
{
    if (path != NULL) {
        unlink(path);
        free(path);
    }

    return;
}

void
test_file_class(void)
{
    char *path = NULL;
    char *name = NULL;
    char self[PATH_MAX];
    struct stat sb;

    memset(self, 0, sizeof(self));

    /* nothing to look at */
    set_mode(&sb, S_IFREG | 0644);
    TARPM_ASSERT_PTR_NULL(file_class(NULL, "/usr/bin/ls", &sb));
    TARPM_ASSERT_PTR_NULL(file_class("/usr/bin/ls", NULL, &sb));
    TARPM_ASSERT_PTR_NULL(file_class("/usr/bin/ls", "/usr/bin/ls", NULL));

    /*
     * The odd file types get a fixed name without libmagic ever being
     * asked, and only a directory holds a token rpm keeps.
     */
    set_mode(&sb, S_IFDIR | 0755);
    ASSERT_AND_FREE(file_class("/usr/share/doc/foo", tmpdir, &sb), RPM_FILE_CLASS_DIR);

    set_mode(&sb, S_IFCHR | 0666);
    ASSERT_AND_FREE(file_class("/usr/lib/foo/dev", tmpdir, &sb), RPM_FILE_CLASS_NONE);

    set_mode(&sb, S_IFBLK | 0660);
    ASSERT_AND_FREE(file_class("/usr/lib/foo/disk", tmpdir, &sb), RPM_FILE_CLASS_NONE);

    set_mode(&sb, S_IFIFO | 0644);
    ASSERT_AND_FREE(file_class("/run/foo.fifo", tmpdir, &sb), RPM_FILE_CLASS_NONE);

    set_mode(&sb, S_IFSOCK | 0755);
    ASSERT_AND_FREE(file_class("/run/foo.sock", tmpdir, &sb), RPM_FILE_CLASS_NONE);

    /*
     * A path ending we answer ourselves wins over what libmagic would
     * say about the bytes.  These files hold plain text, so libmagic
     * would call them text rather than an image or a document.
     */
    set_mode(&sb, S_IFREG | 0644);

    path = make_file("foo.png", "this is not really a PNG\n");
    ASSERT_AND_FREE(file_class("/usr/share/icons/foo.png", path, &sb), "PNG image data");
    drop_file(path);

    path = make_file("foo.html", "this is not really HTML\n");
    ASSERT_AND_FREE(file_class("/usr/share/doc/foo.html", path, &sb), "HTML document");
    drop_file(path);

    /* the ending is answered but the answer holds no token, so no class */
    path = make_file("foo.h", "#define FOO 1\n");
    ASSERT_AND_FREE(file_class("/usr/include/foo.h", path, &sb), RPM_FILE_CLASS_NONE);
    drop_file(path);

    /* everything under /dev/ gets no class, even a known ending */
    path = make_file("null.png", "this is not really a PNG\n");
    ASSERT_AND_FREE(file_class("/dev/null.png", path, &sb), RPM_FILE_CLASS_NONE);
    ASSERT_AND_FREE(file_class("/dev/x", path, &sb), RPM_FILE_CLASS_NONE);
    drop_file(path);

    /* a path that only looks like it is under /dev/ is classified as usual */
    path = make_file("bar.png", "this is not really a PNG\n");
    ASSERT_AND_FREE(file_class("/usr/dev/bar.png", path, &sb), "PNG image data");
    ASSERT_AND_FREE(file_class("/devices/bar.png", path, &sb), "PNG image data");
    drop_file(path);

    /*
     * The shortest path rpm treats as being under /dev/ is "/dev/" and
     * one more character, so "/dev/" on its own is classified as usual.
     */
    path = make_file("plain.txt", "hello world\n");
    name = file_class("/dev/", path, &sb);
    TARPM_ASSERT_PTR_NOT_NULL(name);
    TARPM_ASSERT_PTR_NOT_NULL(strstr(name, "text"));
    free(name);

    /* the rest go to libmagic */
    name = file_class("/usr/share/doc/plain.txt", path, &sb);
    TARPM_ASSERT_PTR_NOT_NULL(name);
    TARPM_ASSERT_PTR_NOT_NULL(strstr(name, "text"));
    free(name);
    drop_file(path);

    /* an empty file is called empty, which rpm keeps */
    path = make_file("empty.txt", NULL);
    ASSERT_AND_FREE(file_class("/usr/share/doc/empty.txt", path, &sb), "empty");
    drop_file(path);

    /* an ELF binary keeps the whole string libmagic hands back */
    set_mode(&sb, S_IFREG | 0755);

    if (readlink("/proc/self/exe", self, sizeof(self) - 1) > 0) {
        name = file_class("/usr/bin/test-class", self, &sb);
        TARPM_ASSERT_PTR_NOT_NULL(name);
        TARPM_ASSERT_PTR_NOT_NULL(strstr(name, "ELF "));
        free(name);
    }

    /*
     * libmagic describes a symlink rather than what it points at, and
     * that description holds no token, so a symlink gets no class.
     */
    set_mode(&sb, S_IFLNK | 0777);
    path = NULL;
    xasprintf(&path, "%s/link", tmpdir);
    TARPM_ASSERT_EQUAL(symlink("/usr/bin/ls", path), 0);
    ASSERT_AND_FREE(file_class("/usr/bin/link", path, &sb), RPM_FILE_CLASS_NONE);
    drop_file(path);

    /* nothing there to look at means nothing to say about it */
    set_mode(&sb, S_IFREG | 0644);
    path = NULL;
    xasprintf(&path, "%s/gone", tmpdir);
    ASSERT_AND_FREE(file_class("/usr/share/doc/gone", path, &sb), RPM_FILE_CLASS_NONE);
    free(path);

    /*
     * Closing the handle in the middle is fine, the next call opens a
     * new one and gets the same answer.
     */
    close_magic();
    close_magic();
    path = make_file("again.txt", "hello world\n");
    name = file_class("/usr/share/doc/again.txt", path, &sb);
    TARPM_ASSERT_PTR_NOT_NULL(name);
    TARPM_ASSERT_PTR_NOT_NULL(strstr(name, "text"));
    free(name);
    drop_file(path);

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("class", init_test_class, clean_test_class);

    if (pSuite == NULL) {
        return NULL;
    }

    /* somewhere to put the files the file_class() tests look at */
    if (mkdtemp(tmpdir) == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test wanted_class()", test_wanted_class) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test skipped_class()", test_skipped_class) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test file_class()", test_file_class) == NULL) {
        return NULL;
    }

    return pSuite;
}
