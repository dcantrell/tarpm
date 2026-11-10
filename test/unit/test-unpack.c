/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

int
init_test_unpack(void)
{
    return 0;
}

int
clean_test_unpack(void)
{
    return 0;
}

void
test_unpack_archive(void)
{
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, true) == -1);
    TARPM_ASSERT_TRUE(unpack_archive(NULL, NULL, false) == -1);

    return;
}

/* add an empty entry owned by us to a cpio archive */
static void
add_entry(struct archive *a, const char *path, const mode_t mode)
{
    struct archive_entry *entry = NULL;

    entry = archive_entry_new();
    TARPM_ASSERT_TRUE(entry != NULL);
    archive_entry_set_pathname(entry, path);
    archive_entry_set_mode(entry, mode);
    archive_entry_set_uid(entry, getuid());
    archive_entry_set_gid(entry, getgid());
    archive_entry_set_size(entry, 0);
    TARPM_ASSERT_TRUE(archive_write_header(a, entry) == ARCHIVE_OK);
    archive_entry_free(entry);
    return;
}

void
test_unpack_archive_modes(void)
{
    char tmpdir[] = "/tmp/tarpm-test-unpack-XXXXXX";
    char *cpio = NULL;
    char *dest = NULL;
    char *path = NULL;
    struct archive *a = NULL;
    struct stat sb;

    TARPM_ASSERT_TRUE(mkdtemp(tmpdir) != NULL);
    cpio = joinpath(tmpdir, "payload.cpio", NULL);
    dest = joinpath(tmpdir, "dest", NULL);
    TARPM_ASSERT_TRUE(mkdir(dest, 0755) == 0);

    /* the entries are ours so the setuid and setgid bits would stay */
    a = archive_write_new();
    TARPM_ASSERT_TRUE(a != NULL);
    TARPM_ASSERT_TRUE(archive_write_set_format_cpio_newc(a) == ARCHIVE_OK);
    TARPM_ASSERT_TRUE(archive_write_open_filename(a, cpio) == ARCHIVE_OK);
    add_entry(a, "./suid", S_IFREG | 06755);
    add_entry(a, "./sgid", S_IFDIR | 02775);
    TARPM_ASSERT_TRUE(archive_write_close(a) == ARCHIVE_OK);
    archive_write_free(a);

    TARPM_ASSERT_TRUE(unpack_archive(cpio, dest, false) == 0);

    path = joinpath(dest, "suid", NULL);
    TARPM_ASSERT_TRUE(stat(path, &sb) == 0);
    TARPM_ASSERT_TRUE(S_ISREG(sb.st_mode));
    TARPM_ASSERT_TRUE((sb.st_mode & 07777) == 0755);
    TARPM_ASSERT_TRUE(unlink(path) == 0);
    free(path);

    path = joinpath(dest, "sgid", NULL);
    TARPM_ASSERT_TRUE(stat(path, &sb) == 0);
    TARPM_ASSERT_TRUE(S_ISDIR(sb.st_mode));
    TARPM_ASSERT_TRUE((sb.st_mode & 07777) == 0775);
    TARPM_ASSERT_TRUE(rmdir(path) == 0);
    free(path);

    TARPM_ASSERT_TRUE(rmdir(dest) == 0);
    TARPM_ASSERT_TRUE(unlink(cpio) == 0);
    TARPM_ASSERT_TRUE(rmdir(tmpdir) == 0);
    free(dest);
    free(cpio);
    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("unpack", init_test_unpack, clean_test_unpack);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test unpack_archive()", test_unpack_archive) == NULL) {
        return NULL;
    }

    if (CU_add_test(pSuite, "test unpack_archive() modes", test_unpack_archive_modes) == NULL) {
        return NULL;
    }

    return pSuite;
}
