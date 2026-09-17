/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <arpa/inet.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmfiles.h>
#include <json.h>
#include <CUnit/Basic.h>
#include "tarpm.h"

#include "test-main.h"

/*
 * Offsets in to the synthetic header data buffer used by the
 * generate_files() tests.  The layout is:
 *
 *     "/usr/bin/\0"                    dirnames    (10 bytes)
 *     "ls\0cat\0"                      basenames   ( 7 bytes)
 *     0, 0                             dirindexes  ( 8 bytes)
 *     1024, 2048                       filesizes   ( 8 bytes)
 *     0100755, 0100755                 filemodes   ( 4 bytes)
 *     "en|de\0\0"                      filelangs   ( 7 bytes)
 *     2, 0                             filecolors  ( 8 bytes)
 *     1, 0                             fileflags   ( 8 bytes)
 *     0xffffffff, 0                    fileverify  ( 8 bytes)
 */
#define DIRNAMES_OFFSET    0
#define BASENAMES_OFFSET   10
#define DIRINDEXES_OFFSET  17
#define FILESIZES_OFFSET   25
#define FILEMODES_OFFSET   33
#define FILELANGS_OFFSET   37
#define FILECOLORS_OFFSET  44
#define FILEFLAGS_OFFSET   52
#define FILEVERIFY_OFFSET  60
#define DATA_SIZE          68
#define NUM_ENTRIES        9

int
init_test_files(void)
{
    return 0;
}

int
clean_test_files(void)
{
    return 0;
}

/* Fill out a single header index entry in network byte order */
static void
set_entry(struct rpmhdrentry *entry, uint32_t tag, uint32_t type, uint32_t offset, uint32_t count)
{
    entry->tag = htonl(tag);
    entry->type = htonl(type);
    entry->offset = (int32_t) htonl(offset);
    entry->count = htonl(count);
    return;
}

/* Build the synthetic header used by the generate_files() tests */
static void
build_header(struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *entries, uint8_t *data)
{
    uint32_t val32 = 0;
    uint16_t val16 = 0;

    memset(hdr, 0, sizeof(*hdr));
    memset(hdrinfo, 0, sizeof(*hdrinfo));
    memset(entries, 0, sizeof(*entries) * NUM_ENTRIES);
    memset(data, 0, DATA_SIZE);

    /* dirnames */
    memcpy(data + DIRNAMES_OFFSET, "/usr/bin/", 10);

    /* basenames */
    memcpy(data + BASENAMES_OFFSET, "ls\0cat", 7);

    /* dirindexes */
    val32 = htonl(0);
    memcpy(data + DIRINDEXES_OFFSET, &val32, sizeof(val32));
    memcpy(data + DIRINDEXES_OFFSET + sizeof(val32), &val32, sizeof(val32));

    /* filesizes */
    val32 = htonl(1024);
    memcpy(data + FILESIZES_OFFSET, &val32, sizeof(val32));
    val32 = htonl(2048);
    memcpy(data + FILESIZES_OFFSET + sizeof(val32), &val32, sizeof(val32));

    /* filemodes */
    val16 = htons(S_IFREG | 0755);
    memcpy(data + FILEMODES_OFFSET, &val16, sizeof(val16));
    memcpy(data + FILEMODES_OFFSET + sizeof(val16), &val16, sizeof(val16));

    /* filelangs (the second entry is the empty string) */
    memcpy(data + FILELANGS_OFFSET, "en|de", 6);

    /* filecolors (the second entry has no color) */
    val32 = htonl(2);
    memcpy(data + FILECOLORS_OFFSET, &val32, sizeof(val32));
    val32 = htonl(0);
    memcpy(data + FILECOLORS_OFFSET + sizeof(val32), &val32, sizeof(val32));

    /* fileflags (the second entry has no flags) */
    val32 = htonl(RPMFILE_CONFIG);
    memcpy(data + FILEFLAGS_OFFSET, &val32, sizeof(val32));
    val32 = htonl(0);
    memcpy(data + FILEFLAGS_OFFSET + sizeof(val32), &val32, sizeof(val32));

    /*
     * fileverifyflags (the second entry verifies nothing).  The first
     * entry is what rpmbuild writes for %verify(not md5 mtime), which
     * is the complement of the named bits and so carries the bits rpm
     * keeps for itself; those are expected to be normalized away.
     */
    val32 = htonl(RPMVERIFY_ALL & ~(RPMVERIFY_FILEDIGEST | RPMVERIFY_MTIME));
    memcpy(data + FILEVERIFY_OFFSET, &val32, sizeof(val32));
    val32 = htonl(0);
    memcpy(data + FILEVERIFY_OFFSET + sizeof(val32), &val32, sizeof(val32));

    set_entry(&entries[0], RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE, DIRNAMES_OFFSET, 1);
    set_entry(&entries[1], RPMTAG_BASENAMES, RPM_STRING_ARRAY_TYPE, BASENAMES_OFFSET, 2);
    set_entry(&entries[2], RPMTAG_DIRINDEXES, RPM_INT32_TYPE, DIRINDEXES_OFFSET, 2);
    set_entry(&entries[3], RPMTAG_FILESIZES, RPM_INT32_TYPE, FILESIZES_OFFSET, 2);
    set_entry(&entries[4], RPMTAG_FILEMODES, RPM_INT16_TYPE, FILEMODES_OFFSET, 2);
    set_entry(&entries[5], RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE, FILELANGS_OFFSET, 2);
    set_entry(&entries[6], RPMTAG_FILECOLORS, RPM_INT32_TYPE, FILECOLORS_OFFSET, 2);
    set_entry(&entries[7], RPMTAG_FILEFLAGS, RPM_INT32_TYPE, FILEFLAGS_OFFSET, 2);
    set_entry(&entries[8], RPMTAG_FILEVERIFYFLAGS, RPM_INT32_TYPE, FILEVERIFY_OFFSET, 2);

    hdr->nentries = NUM_ENTRIES;
    hdrinfo->estart = entries;
    hdrinfo->datastart = data;

    return;
}

/* Add a file entry with the given path to a files array */
static struct json_object *
add_file(struct json_object *files, const char *path)
{
    struct json_object *file = NULL;

    file = json_object_new_object();
    json_object_object_add(file, "path", json_object_new_string(path));
    json_object_array_add(files, file);

    return file;
}

/* Return the value array of the named tag in a tags array */
static struct json_object *
get_tag_values(struct json_object *tags, const char *name)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *tag = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (!json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tag)) {
            continue;
        }

        if (strcmp(json_object_get_string(tag), name)) {
            continue;
        }

        if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
            return value;
        }
    }

    return NULL;
}

/* Test generate_files() with NULL inputs */
void
test_generate_files_null(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;

    memset(&hdr, 0, sizeof(hdr));
    memset(&hdrinfo, 0, sizeof(hdrinfo));

    TARPM_ASSERT_PTR_NULL(generate_files(NULL, NULL, NULL));
    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, NULL, NULL));
    TARPM_ASSERT_PTR_NULL(generate_files(NULL, &hdrinfo, NULL));

    return;
}

/* Test generate_files() with a header that carries no file list */
void
test_generate_files_no_file_list(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[NUM_ENTRIES];
    uint8_t data[DATA_SIZE];

    build_header(&hdr, &hdrinfo, entries, data);

    /* drop the basenames entry so the required tags are incomplete */
    set_entry(&entries[1], RPMTAG_NAME, RPM_STRING_TYPE, DIRNAMES_OFFSET, 1);

    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, &hdrinfo, NULL));

    return;
}

/* Test generate_files() with file lists of differing lengths */
void
test_generate_files_mismatched_lengths(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[NUM_ENTRIES];
    uint8_t data[DATA_SIZE];

    build_header(&hdr, &hdrinfo, entries, data);

    /* two basenames but only one dirindex */
    set_entry(&entries[2], RPMTAG_DIRINDEXES, RPM_INT32_TYPE, DIRINDEXES_OFFSET, 1);

    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, &hdrinfo, NULL));

    return;
}

/* Test generate_files() with a valid file list */
void
test_generate_files_valid(void)
{
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[NUM_ENTRIES];
    uint8_t data[DATA_SIZE];
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *value = NULL;
    struct json_object *langs = NULL;
    struct json_object *colors = NULL;
    struct json_object *flags = NULL;
    struct json_object *verifyflags = NULL;

    build_header(&hdr, &hdrinfo, entries, data);

    files = generate_files(&hdr, &hdrinfo, NULL);
    TARPM_ASSERT_PTR_NOT_NULL(files);
    TARPM_ASSERT_EQUAL(json_object_array_length(files), 2);

    /* the first file carries two languages */
    file = json_object_array_get_idx(files, 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "path", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "/usr/bin/ls");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "size", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int64(value), 1024);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "mode", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "0755");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "langs", &langs));
    TARPM_ASSERT_EQUAL(json_object_array_length(langs), 2);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(langs, 0)), "en");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(langs, 1)), "de");

    /* the first file is a 64 bit ELF object */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "colors", &colors));
    TARPM_ASSERT_EQUAL(json_object_array_length(colors), 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(colors, 0)), "Elf64");

    /* the first file is a %config file */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "flags", &flags));
    TARPM_ASSERT_EQUAL(json_object_array_length(flags), 1);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(flags, 0)), "config");

    /* the first file verifies its digest and mtime */
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "verifyflags", &verifyflags));
    TARPM_ASSERT_EQUAL(json_object_array_length(verifyflags), 7);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 0)), "filesize");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 1)), "linkto");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 2)), "user");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 3)), "group");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 4)), "mode");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 5)), "rdev");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(verifyflags, 6)), "caps");

    /* the second file has an empty language string, so no langs key */
    file = json_object_array_get_idx(files, 1);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "path", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "/usr/bin/cat");
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "size", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int64(value), 2048);
    TARPM_ASSERT_FALSE(json_object_object_get_ex(file, "langs", &langs));

    /* the second file has no color, so no colors key */
    TARPM_ASSERT_FALSE(json_object_object_get_ex(file, "colors", &colors));

    /* the second file has no flags, so no flags key */
    TARPM_ASSERT_FALSE(json_object_object_get_ex(file, "flags", &flags));

    /* the second file verifies nothing, so no verifyflags key */
    TARPM_ASSERT_FALSE(json_object_object_get_ex(file, "verifyflags", &verifyflags));

    json_object_put(files);

    return;
}

/* The file types and the names the "files" array gives them */
static const mode_t type_bits_list[] = { S_IFIFO, S_IFCHR, S_IFDIR, S_IFBLK, S_IFREG, S_IFLNK, S_IFSOCK };
static const char *type_name_list[] = { "pipe", "chardev", "dir", "blockdev", "file", "symlink", "socket" };
static const size_t ntypes = sizeof(type_bits_list) / sizeof(type_bits_list[0]);

/* Test generate_files() names the file type the mode bits carry */
void
test_generate_files_types(void)
{
    size_t i = 0;
    struct rpmhdr hdr;
    struct rpmhdrinfo hdrinfo;
    struct rpmhdrentry entries[NUM_ENTRIES];
    uint8_t data[DATA_SIZE];
    uint16_t val16 = 0;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < ntypes; i++) {
        build_header(&hdr, &hdrinfo, entries, data);

        /* the second file takes the type under test */
        val16 = htons(type_bits_list[i] | 0644);
        memcpy(data + FILEMODES_OFFSET + sizeof(val16), &val16, sizeof(val16));

        files = generate_files(&hdr, &hdrinfo, NULL);
        TARPM_ASSERT_PTR_NOT_NULL(files);
        TARPM_ASSERT_EQUAL(json_object_array_length(files), 2);

        /* the first file is left a regular file */
        file = json_object_array_get_idx(files, 0);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "file");

        file = json_object_array_get_idx(files, 1);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), type_name_list[i]);

        /* the permissions keep to themselves */
        TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "mode", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "0644");

        json_object_put(files);
    }

    return;
}

/* Test add_file_list_tags() with NULL inputs */
void
test_add_file_list_tags_null(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;

    /* all NULL should not crash */
    add_file_list_tags(NULL, NULL, NULL, NULL, NULL);

    /* NULL files should add no tags */
    tags = json_object_new_array();
    add_file_list_tags(tags, NULL, NULL, NULL, NULL);
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    /* NULL tags should not crash */
    files = json_object_new_array();
    add_file_list_tags(NULL, files, NULL, NULL, NULL);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with an empty files array */
void
test_add_file_list_tags_empty(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* an empty files array should add no tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with an invalid files type */
void
test_add_file_list_tags_invalid_type(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;

    tags = json_object_new_array();
    files = json_object_new_string("not an array");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* an invalid type should add no tags */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with a small file list */
void
test_add_file_list_tags_file_list(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    file = add_file(files, "/usr/bin/ls");
    json_object_object_add(file, "user", json_object_new_string("root"));
    json_object_object_add(file, "group", json_object_new_string("root"));
    json_object_object_add(file, "digest", json_object_new_string("abc123"));

    file = add_file(files, "/usr/share/man/man1/ls.1");
    json_object_object_add(file, "linkto", json_object_new_string("/usr/bin/ls"));

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* all nineteen file list tags should be present */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 19);

    /* basenames come from the last path component */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_BASENAMES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "ls");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "ls.1");

    /* dirnames hold the unique directories in order of first appearance */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_DIRNAMES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "/usr/bin/");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "/usr/share/man/man1/");

    /* dirindexes point in to the dirnames array */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_DIRINDEXES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 0)), 0);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 1)), 1);

    /* missing digests carry an empty string */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDIGESTS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "abc123");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "");

    /* missing link targets carry an empty string */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILELINKTOS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "/usr/bin/ls");

    /* missing users and groups default to root */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEUSERNAME));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "root");

    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEGROUPNAME));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "root");

    /* missing inodes are numbered sequentially starting at one */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEINODES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 0)), 1);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 1)), 2);

    /* no file generated a dependency, so the dictionary tags are left off */
    TARPM_ASSERT_PTR_NULL(get_tag_values(tags, rpmTagGetName(RPMTAG_DEPENDSDICT)));
    TARPM_ASSERT_PTR_NULL(get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDEPENDSX)));
    TARPM_ASSERT_PTR_NULL(get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDEPENDSN)));

    json_object_put(tags);
    json_object_put(files);

    return;
}

/*
 * Helper for test_add_file_list_tags_sizes() below that creates a
 * directory in the payload tree of a test extraction directory.
 */
static void
mkdir_payload(const char *input_dir, const char *path)
{
    char *dir_path = NULL;

    dir_path = joinpath(input_dir, PAYLOAD_SUBDIR, path, NULL);
    TARPM_ASSERT_TRUE(mkdirp(dir_path, 0755) == 0);
    free(dir_path);

    return;
}

/*
 * Helper for test_add_file_list_tags_sizes() below that writes a file
 * of the given size in to the payload tree of a test extraction
 * directory.
 */
static void
write_payload(const char *input_dir, const char *path, const size_t size)
{
    size_t i = 0;
    char *file_path = NULL;
    FILE *fp = NULL;

    file_path = joinpath(input_dir, PAYLOAD_SUBDIR, path, NULL);
    fp = fopen(file_path, "wb");
    TARPM_ASSERT_PTR_NOT_NULL(fp);

    for (i = 0; i < size; i++) {
        TARPM_ASSERT_TRUE(fputc('x', fp) != EOF);
    }

    TARPM_ASSERT_TRUE(fclose(fp) == 0);
    free(file_path);

    return;
}

/*
 * Helper for test_add_file_list_tags_sizes() below that creates a
 * symlink in the payload tree of a test extraction directory.
 */
static void
link_payload(const char *input_dir, const char *path, const char *target)
{
    char *file_path = NULL;

    file_path = joinpath(input_dir, PAYLOAD_SUBDIR, path, NULL);
    TARPM_ASSERT_TRUE(symlink(target, file_path) == 0);
    free(file_path);

    return;
}

/*
 * Helper for test_add_file_list_tags_mismatch_rdev() below that
 * creates a pipe in the payload tree of a test extraction directory.
 */
static void
fifo_payload(const char *input_dir, const char *path)
{
    char *file_path = NULL;

    file_path = joinpath(input_dir, PAYLOAD_SUBDIR, path, NULL);
    TARPM_ASSERT_TRUE(mkfifo(file_path, 0600) == 0);
    free(file_path);

    return;
}

/*
 * Helper for test_add_file_list_tags_sizes() below that removes a file
 * or a directory from the payload tree of a test extraction directory.
 */
static void
remove_payload(const char *input_dir, const char *path)
{
    char *file_path = NULL;
    struct stat sb;

    file_path = joinpath(input_dir, PAYLOAD_SUBDIR, path, NULL);
    TARPM_ASSERT_TRUE(lstat(file_path, &sb) == 0);

    if (S_ISDIR(sb.st_mode)) {
        TARPM_ASSERT_TRUE(rmdir(file_path) == 0);
    } else {
        TARPM_ASSERT_TRUE(unlink(file_path) == 0);
    }

    free(file_path);

    return;
}

/* Test add_file_list_tags() takes the file sizes from the payload */
void
test_add_file_list_tags_sizes(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *flags = NULL;
    struct json_object *values = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    /* the payload tree holds what the file list describes */
    mkdir_payload(input_dir, "/usr/bin");
    write_payload(input_dir, "/usr/bin/grown", 4096);
    write_payload(input_dir, "/usr/bin/shrunk", 3);
    link_payload(input_dir, "/usr/bin/link", "grown");

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a file that got bigger since the package was unpacked */
    file = add_file(files, "/usr/bin/grown");
    json_object_object_add(file, "size", json_object_new_int64(10));

    /* and one that got smaller */
    file = add_file(files, "/usr/bin/shrunk");
    json_object_object_add(file, "size", json_object_new_int64(99));

    /* a symlink keeps the length of the target string the header carries */
    file = add_file(files, "/usr/bin/link");
    json_object_object_add(file, "size", json_object_new_int64(11));
    json_object_object_add(file, "linkto", json_object_new_string("/usr/bin/ls"));

    /* a %ghost file is never in the payload, so it keeps its size */
    file = add_file(files, "/usr/bin/ghost.log");
    json_object_object_add(file, "size", json_object_new_int64(77));
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("ghost"));
    json_object_object_add(file, "flags", flags);

    /* a directory carries no size at all */
    add_file(files, "/usr/bin");

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    /* the regular files are measured and everything else is left alone */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 5);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 4096);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 2)), 11);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 3)), 77);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 4)), 0);

    json_object_put(tags);
    json_object_put(files);

    /* clean up the payload tree */
    remove_payload(input_dir, "/usr/bin/grown");
    remove_payload(input_dir, "/usr/bin/shrunk");
    remove_payload(input_dir, "/usr/bin/link");
    remove_payload(input_dir, "/usr/bin");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/*
 * Test add_file_list_tags() keeps the recorded sizes when there is no
 * payload tree to measure.
 */
void
test_add_file_list_tags_sizes_no_payload(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    file = add_file(files, "/usr/bin/ls");
    json_object_object_add(file, "size", json_object_new_int64(1024));

    add_file(files, "/usr/bin");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 1024);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 0);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() builds the mode out of the type and the permissions */
void
test_add_file_list_tags_types(void)
{
    size_t i = 0;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    for (i = 0; i < ntypes; i++) {
        file = add_file(files, type_name_list[i]);
        json_object_object_add(file, "mode", json_object_new_string("0644"));
        json_object_object_add(file, "type", json_object_new_string(type_name_list[i]));
    }

    /* a type nobody knows falls back to the guess tarpm used to make */
    file = add_file(files, "mystery");
    json_object_object_add(file, "mode", json_object_new_string("0644"));
    json_object_object_add(file, "type", json_object_new_string("wormhole"));
    json_object_object_add(file, "size", json_object_new_int64(3));

    /* so does an entry from an older file list that names no type */
    file = add_file(files, "notype");
    json_object_object_add(file, "mode", json_object_new_string("0755"));

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEMODES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), ntypes + 2);

    for (i = 0; i < ntypes; i++) {
        TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, i)), (int) (type_bits_list[i] | 0644));
    }

    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, ntypes)), (int) (S_IFREG | 0644));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, ntypes + 1)), (int) (S_IFDIR | 0755));

    json_object_put(tags);
    json_object_put(files);

    return;
}

/*
 * Test add_file_list_tags() takes the file type from the payload when
 * there is a file there to look at.
 */
void
test_add_file_list_tags_type_from_payload(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *flags = NULL;
    struct json_object *values = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    mkdir_payload(input_dir, "/usr/bin");
    write_payload(input_dir, "/usr/bin/plain", 3);
    link_payload(input_dir, "/usr/bin/link", "plain");

    tags = json_object_new_array();
    files = json_object_new_array();

    /* the payload has a directory no matter what the entry says */
    file = add_file(files, "/usr/bin");
    json_object_object_add(file, "mode", json_object_new_string("0755"));
    json_object_object_add(file, "type", json_object_new_string("file"));

    file = add_file(files, "/usr/bin/plain");
    json_object_object_add(file, "mode", json_object_new_string("0644"));
    json_object_object_add(file, "type", json_object_new_string("file"));
    json_object_object_add(file, "size", json_object_new_int64(3));

    file = add_file(files, "/usr/bin/link");
    json_object_object_add(file, "mode", json_object_new_string("0777"));
    json_object_object_add(file, "type", json_object_new_string("symlink"));
    json_object_object_add(file, "size", json_object_new_int64(5));

    /* a %ghost is never in the payload, so its type is all there is */
    file = add_file(files, "/usr/bin/ghostdir");
    json_object_object_add(file, "mode", json_object_new_string("0700"));
    json_object_object_add(file, "type", json_object_new_string("dir"));
    json_object_object_add(file, "size", json_object_new_int64(9));
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("ghost"));
    json_object_object_add(file, "flags", flags);

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEMODES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 4);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 0)), (int) (S_IFDIR | 0755));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 1)), (int) (S_IFREG | 0644));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 2)), (int) (S_IFLNK | 0777));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 3)), (int) (S_IFDIR | 0700));

    json_object_put(tags);
    json_object_put(files);

    remove_payload(input_dir, "/usr/bin/plain");
    remove_payload(input_dir, "/usr/bin/link");
    remove_payload(input_dir, "/usr/bin");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/*
 * Test add_file_list_tags() corrects an entry whose type does not
 * match what the payload tree holds.
 */
void
test_add_file_list_tags_type_mismatch(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *value = NULL;
    struct json_object *values = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    mkdir_payload(input_dir, "/usr/bin");
    write_payload(input_dir, "/usr/bin/plain", 3);
    link_payload(input_dir, "/usr/bin/link", "plain");

    tags = json_object_new_array();
    files = json_object_new_array();

    /* the payload has a directory here and not a regular file */
    file = add_file(files, "/usr/bin");
    json_object_object_add(file, "mode", json_object_new_string("0755"));
    json_object_object_add(file, "type", json_object_new_string("file"));

    /* and a symlink here and not a regular file */
    file = add_file(files, "/usr/bin/link");
    json_object_object_add(file, "mode", json_object_new_string("0777"));
    json_object_object_add(file, "type", json_object_new_string("file"));

    /* this one agrees and is left alone */
    file = add_file(files, "/usr/bin/plain");
    json_object_object_add(file, "mode", json_object_new_string("0644"));
    json_object_object_add(file, "type", json_object_new_string("file"));
    json_object_object_add(file, "size", json_object_new_int64(3));

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    /* the file list now says what the payload holds */
    file = json_object_array_get_idx(files, 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "dir");

    file = json_object_array_get_idx(files, 1);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "symlink");

    file = json_object_array_get_idx(files, 2);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "file");

    /* and so do the modes */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEMODES));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 0)), (int) (S_IFDIR | 0755));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 1)), (int) (S_IFLNK | 0777));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 2)), (int) (S_IFREG | 0644));

    json_object_put(tags);
    json_object_put(files);

    remove_payload(input_dir, "/usr/bin/plain");
    remove_payload(input_dir, "/usr/bin/link");
    remove_payload(input_dir, "/usr/bin");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/*
 * Test add_file_list_tags() takes the size, the digest and the link
 * target from the payload when the type of an entry changes
 */
void
test_add_file_list_tags_mismatch_values(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *value = NULL;
    struct json_object *sizes = NULL;
    struct json_object *digests = NULL;
    struct json_object *linktos = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    mkdir_payload(input_dir, "/usr/share");
    mkdir_payload(input_dir, "/usr/share/wasfile");
    write_payload(input_dir, "/usr/share/waslink", 5);
    link_payload(input_dir, "/usr/share/wasplain", "target");

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a regular file that is now a directory */
    file = add_file(files, "/usr/share/wasfile");
    json_object_object_add(file, "mode", json_object_new_string("0755"));
    json_object_object_add(file, "type", json_object_new_string("file"));
    json_object_object_add(file, "size", json_object_new_int64(42));
    json_object_object_add(file, "digest", json_object_new_string("0123456789abcdef0123456789abcdef"));

    /* a symlink that is now a regular file */
    file = add_file(files, "/usr/share/waslink");
    json_object_object_add(file, "mode", json_object_new_string("0644"));
    json_object_object_add(file, "type", json_object_new_string("symlink"));
    json_object_object_add(file, "size", json_object_new_int64(3));
    json_object_object_add(file, "linkto", json_object_new_string("old"));

    /* a regular file that is now a symlink */
    file = add_file(files, "/usr/share/wasplain");
    json_object_object_add(file, "mode", json_object_new_string("0777"));
    json_object_object_add(file, "type", json_object_new_string("file"));
    json_object_object_add(file, "size", json_object_new_int64(99));
    json_object_object_add(file, "digest", json_object_new_string("0123456789abcdef0123456789abcdef"));

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    /* the directory keeps none of what it carried as a file */
    file = json_object_array_get_idx(files, 0);
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "size", &value));
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "digest", &value));
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "linkto", &value));

    /* the regular file picks up its size and a digest of its contents */
    file = json_object_array_get_idx(files, 1);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "size", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int64(value), 5);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "digest", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "fb0e22c79ac75679e9881e6ba183b354");
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "linkto", &value));

    /* the symlink picks up its target and the length of it */
    file = json_object_array_get_idx(files, 2);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "size", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int64(value), 6);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "linkto", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "target");
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "digest", &value));

    /* and the tags carry the same */
    sizes = get_tag_values(tags, rpmTagGetName(RPMTAG_FILESIZES));
    TARPM_ASSERT_PTR_NOT_NULL(sizes);
    TARPM_ASSERT_EQUAL(json_object_array_length(sizes), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(sizes, 0)), 0);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(sizes, 1)), 5);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(sizes, 2)), 6);

    digests = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDIGESTS));
    TARPM_ASSERT_PTR_NOT_NULL(digests);
    TARPM_ASSERT_EQUAL(json_object_array_length(digests), 3);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(digests, 0)), "");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(digests, 1)), "fb0e22c79ac75679e9881e6ba183b354");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(digests, 2)), "");

    linktos = get_tag_values(tags, rpmTagGetName(RPMTAG_FILELINKTOS));
    TARPM_ASSERT_PTR_NOT_NULL(linktos);
    TARPM_ASSERT_EQUAL(json_object_array_length(linktos), 3);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(linktos, 0)), "");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(linktos, 1)), "");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(linktos, 2)), "target");

    json_object_put(tags);
    json_object_put(files);

    remove_payload(input_dir, "/usr/share/wasfile");
    remove_payload(input_dir, "/usr/share/waslink");
    remove_payload(input_dir, "/usr/share/wasplain");
    remove_payload(input_dir, "/usr/share");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/*
 * Test add_file_list_tags() takes the device number from the payload
 * when the type of an entry changes
 */
void
test_add_file_list_tags_mismatch_rdev(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    char *file_path = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *value = NULL;
    struct json_object *rdevs = NULL;
    bool have_dev = false;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    mkdir_payload(input_dir, "/dev");
    fifo_payload(input_dir, "/dev/wasdev");

    /*
     * Making a device node takes privileges the test suite usually
     * does not have, so the half of this that needs one only runs
     * when the suite is run as root.
     */
    file_path = joinpath(input_dir, PAYLOAD_SUBDIR, "/dev/isdev", NULL);
    have_dev = (mknod(file_path, S_IFCHR | 0600, makedev(1, 3)) == 0);
    free(file_path);

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a device node that is now a pipe gives up its device number */
    file = add_file(files, "/dev/wasdev");
    json_object_object_add(file, "mode", json_object_new_string("0600"));
    json_object_object_add(file, "type", json_object_new_string("chardev"));
    json_object_object_add(file, "rdev", json_object_new_int(1234));

    if (have_dev) {
        /* and a regular file that is now a device node picks one up */
        file = add_file(files, "/dev/isdev");
        json_object_object_add(file, "mode", json_object_new_string("0600"));
        json_object_object_add(file, "type", json_object_new_string("file"));
        json_object_object_add(file, "size", json_object_new_int64(7));
        json_object_object_add(file, "digest", json_object_new_string("0123456789abcdef0123456789abcdef"));
    }

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    rdevs = get_tag_values(tags, rpmTagGetName(RPMTAG_FILERDEVS));
    TARPM_ASSERT_PTR_NOT_NULL(rdevs);

    file = json_object_array_get_idx(files, 0);
    TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "pipe");
    TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "rdev", &value));
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(rdevs, 0)), 0);

    if (have_dev) {
        file = json_object_array_get_idx(files, 1);
        TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "type", &value));
        TARPM_ASSERT_STRING_EQUAL(json_object_get_string(value), "chardev");
        TARPM_ASSERT_TRUE(json_object_object_get_ex(file, "rdev", &value));
        TARPM_ASSERT_EQUAL(json_object_get_int(value), (int) makedev(1, 3));
        TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "size", &value));
        TARPM_ASSERT_TRUE(!json_object_object_get_ex(file, "digest", &value));
        TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(rdevs, 1)), (int) makedev(1, 3));
    }

    json_object_put(tags);
    json_object_put(files);

    if (have_dev) {
        remove_payload(input_dir, "/dev/isdev");
    }

    remove_payload(input_dir, "/dev/wasdev");
    remove_payload(input_dir, "/dev");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/* Add a tag entry with the given name and value to a tags array */
static void
add_tag(struct json_object *tags, const char *name, const char *value)
{
    struct json_object *tag = NULL;

    tag = json_object_new_object();
    json_object_object_add(tag, "tag", json_object_new_string(name));
    json_object_object_add(tag, "value", json_object_new_string(value));
    json_object_array_add(tags, tag);

    return;
}

/* Test add_file_list_tags() adds up the installed size of the package */
void
test_add_file_list_tags_total_size(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *flags = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    mkdir_payload(input_dir, "/usr/bin");
    write_payload(input_dir, "/usr/bin/one", 4096);
    write_payload(input_dir, "/usr/bin/two", 3);
    write_payload(input_dir, "/usr/bin/hard1", 100);
    write_payload(input_dir, "/usr/bin/hard2", 100);
    link_payload(input_dir, "/usr/bin/link", "one");

    files = json_object_new_array();

    file = add_file(files, "/usr/bin/one");
    json_object_object_add(file, "size", json_object_new_int64(10));

    file = add_file(files, "/usr/bin/two");
    json_object_object_add(file, "size", json_object_new_int64(99));

    /* hard links of one another only take up the space once */
    file = add_file(files, "/usr/bin/hard1");
    json_object_object_add(file, "size", json_object_new_int64(100));
    json_object_object_add(file, "inode", json_object_new_int(42));

    file = add_file(files, "/usr/bin/hard2");
    json_object_object_add(file, "size", json_object_new_int64(100));
    json_object_object_add(file, "inode", json_object_new_int(42));

    /* a symlink counts the length of the target string */
    file = add_file(files, "/usr/bin/link");
    json_object_object_add(file, "size", json_object_new_int64(3));
    json_object_object_add(file, "linkto", json_object_new_string("one"));

    /* a %ghost file is never in the payload but still counts */
    file = add_file(files, "/usr/bin/ghost.log");
    json_object_object_add(file, "size", json_object_new_int64(77));
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("ghost"));
    json_object_object_add(file, "flags", flags);

    /* a directory adds nothing */
    add_file(files, "/usr/bin");

    /* the recorded size is out of date and gets replaced */
    tags = json_object_new_array();
    add_tag(tags, rpmTagGetName(RPMTAG_SIZE), "1");

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    TARPM_ASSERT_TRUE(!strcmp(get_tag_value(tags, rpmTagGetName(RPMTAG_SIZE)), "4279"));

    json_object_put(tags);

    /* a header using the long form of the tag gets the same number */
    tags = json_object_new_array();
    add_tag(tags, rpmTagGetName(RPMTAG_LONGSIZE), "1");

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    TARPM_ASSERT_TRUE(!strcmp(get_tag_value(tags, rpmTagGetName(RPMTAG_LONGSIZE)), "4279"));

    json_object_put(tags);

    /* a header carrying neither tag does not gain one */
    tags = json_object_new_array();

    add_file_list_tags(tags, files, input_dir, PAYLOAD_SUBDIR, NULL);

    TARPM_ASSERT_TRUE(get_tag_value(tags, rpmTagGetName(RPMTAG_SIZE)) == NULL);
    TARPM_ASSERT_TRUE(get_tag_value(tags, rpmTagGetName(RPMTAG_LONGSIZE)) == NULL);

    json_object_put(tags);
    json_object_put(files);

    remove_payload(input_dir, "/usr/bin/one");
    remove_payload(input_dir, "/usr/bin/two");
    remove_payload(input_dir, "/usr/bin/hard1");
    remove_payload(input_dir, "/usr/bin/hard2");
    remove_payload(input_dir, "/usr/bin/link");
    remove_payload(input_dir, "/usr/bin");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/* Return the entry with the given path from a files array */
static struct json_object *
find_file(struct json_object *files, const char *path)
{
    size_t i = 0;
    struct json_object *file = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < json_object_array_length(files); i++) {
        file = json_object_array_get_idx(files, i);

        if (json_object_object_get_ex(file, "path", &value) && !strcmp(json_object_get_string(value), path)) {
            return file;
        }
    }

    return NULL;
}

/* Return the string value of a key in a files array entry */
static const char *
file_str(struct json_object *file, const char *key)
{
    struct json_object *value = NULL;

    if (file == NULL || !json_object_object_get_ex(file, key, &value)) {
        return NULL;
    }

    return json_object_get_string(value);
}

/* Test add_payload_files() with NULL and bad inputs */
void
test_add_payload_files_null(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    struct json_object *tags = NULL;
    struct json_object *files = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    tags = json_object_new_array();
    files = json_object_new_array();
    add_file(files, "/usr/bin/ls");

    add_payload_files(NULL, files, input_dir, PAYLOAD_SUBDIR);
    add_payload_files(tags, NULL, input_dir, PAYLOAD_SUBDIR);
    add_payload_files(tags, files, NULL, PAYLOAD_SUBDIR);
    add_payload_files(tags, files, input_dir, NULL);

    /* and there is no payload tree here to walk */
    add_payload_files(tags, files, input_dir, PAYLOAD_SUBDIR);

    TARPM_ASSERT_EQUAL(json_object_array_length(files), 1);

    json_object_put(files);

    /* the files value has to be an array */
    files = json_object_new_object();
    add_payload_files(tags, files, input_dir, PAYLOAD_SUBDIR);

    json_object_put(tags);
    json_object_put(files);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/* Test add_payload_files() picks up what was added to the payload */
void
test_add_payload_files_new(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    /* the payload tree holds more than the file list names */
    mkdir_payload(input_dir, "/usr/bin");
    write_payload(input_dir, "/usr/bin/known", 5);
    write_payload(input_dir, "/usr/bin/added", 12);
    link_payload(input_dir, "/usr/bin/link", "known");
    mkdir_payload(input_dir, "/usr/share/newdir");
    write_payload(input_dir, "/usr/share/newdir/note.txt", 3);

    tags = json_object_new_array();
    files = json_object_new_array();
    add_file(files, "/usr/bin");
    add_file(files, "/usr/bin/known");

    add_payload_files(tags, files, input_dir, PAYLOAD_SUBDIR);

    /*
     * The two entries the list had are joined by the five new ones.
     * "/usr" is left out because it only leads to "/usr/bin", which
     * the list already names.
     */
    TARPM_ASSERT_EQUAL(json_object_array_length(files), 7);
    TARPM_ASSERT_TRUE(find_file(files, "/usr") == NULL);

    /* a new regular file carries its size, mode, mtime, owner and digest */
    file = find_file(files, "/usr/bin/added");
    TARPM_ASSERT_PTR_NOT_NULL(file);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_object_get(file, "size")), 12);
    TARPM_ASSERT_TRUE(file_str(file, "mode") != NULL);
    TARPM_ASSERT_TRUE(file_str(file, "mtime") != NULL);
    TARPM_ASSERT_STRING_EQUAL(file_str(file, "user"), RPM_FILE_DEFAULT_USER);
    TARPM_ASSERT_STRING_EQUAL(file_str(file, "group"), RPM_FILE_DEFAULT_GROUP);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_object_get(file, "device")), RPM_FILE_DEFAULT_DEVICE);

    /* the digest is the MD5 of the twelve bytes written above */
    TARPM_ASSERT_STRING_EQUAL(file_str(file, "digest"), "f94c84fac5cb091c60bb143cb957d229");

    /* a new symlink carries its target and the length of it */
    file = find_file(files, "/usr/bin/link");
    TARPM_ASSERT_PTR_NOT_NULL(file);
    TARPM_ASSERT_STRING_EQUAL(file_str(file, "linkto"), "known");
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_object_get(file, "size")), 5);
    TARPM_ASSERT_TRUE(file_str(file, "digest") == NULL);

    /* a new directory carries no size and no digest */
    file = find_file(files, "/usr/share/newdir");
    TARPM_ASSERT_PTR_NOT_NULL(file);
    TARPM_ASSERT_TRUE(json_object_object_get(file, "size") == NULL);
    TARPM_ASSERT_TRUE(file_str(file, "digest") == NULL);

    /* and the directories come before what they hold */
    TARPM_ASSERT_PTR_NOT_NULL(find_file(files, "/usr/share"));
    TARPM_ASSERT_STRING_EQUAL(file_str(json_object_array_get_idx(files, 4), "path"), "/usr/share");
    TARPM_ASSERT_STRING_EQUAL(file_str(json_object_array_get_idx(files, 5), "path"), "/usr/share/newdir");
    TARPM_ASSERT_STRING_EQUAL(file_str(json_object_array_get_idx(files, 6), "path"), "/usr/share/newdir/note.txt");

    /* a second run finds nothing new */
    add_payload_files(tags, files, input_dir, PAYLOAD_SUBDIR);
    TARPM_ASSERT_EQUAL(json_object_array_length(files), 7);

    json_object_put(tags);
    json_object_put(files);

    /* clean up the payload tree */
    remove_payload(input_dir, "/usr/share/newdir/note.txt");
    remove_payload(input_dir, "/usr/share/newdir");
    remove_payload(input_dir, "/usr/share");
    remove_payload(input_dir, "/usr/bin/known");
    remove_payload(input_dir, "/usr/bin/added");
    remove_payload(input_dir, "/usr/bin/link");
    remove_payload(input_dir, "/usr/bin");
    remove_payload(input_dir, "/usr");

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/* Test add_payload_files() keeps the bare names a source RPM uses */
void
test_add_payload_files_source(void)
{
    char input_dir[] = "/tmp/tarpm-test-files-XXXXXX";
    char *payload_dir = NULL;
    struct json_object *tags = NULL;
    struct json_object *tag = NULL;
    struct json_object *files = NULL;

    TARPM_ASSERT_TRUE(mkdtemp(input_dir) != NULL);

    payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    TARPM_ASSERT_TRUE(mkdirp(payload_dir, 0755) == 0);

    write_payload(input_dir, "/known.spec", 4);
    write_payload(input_dir, "/added.tar.gz", 8);

    /* a source package says so with a tag */
    tags = json_object_new_array();
    tag = json_object_new_object();
    json_object_object_add(tag, "tag", json_object_new_string(rpmTagGetName(RPMTAG_SOURCEPACKAGE)));
    json_object_object_add(tag, "value", json_object_new_int(1));
    json_object_array_add(tags, tag);

    files = json_object_new_array();
    add_file(files, "known.spec");

    add_payload_files(tags, files, input_dir, PAYLOAD_SUBDIR);

    TARPM_ASSERT_EQUAL(json_object_array_length(files), 2);
    TARPM_ASSERT_PTR_NOT_NULL(find_file(files, "added.tar.gz"));
    TARPM_ASSERT_TRUE(find_file(files, "/added.tar.gz") == NULL);

    json_object_put(tags);
    json_object_put(files);

    remove_payload(input_dir, "/known.spec");
    remove_payload(input_dir, "/added.tar.gz");

    TARPM_ASSERT_TRUE(rmdir(payload_dir) == 0);
    free(payload_dir);

    TARPM_ASSERT_TRUE(rmdir(input_dir) == 0);

    return;
}

/* Test add_file_list_tags() with class values */
void
test_add_file_list_tags_class(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    file = add_file(files, "/usr/bin/ls");
    json_object_object_add(file, "class", json_object_new_string("ELF 64-bit LSB executable"));

    file = add_file(files, "/usr/bin/cat");
    json_object_object_add(file, "class", json_object_new_string("ELF 64-bit LSB executable"));

    add_file(files, "/usr/share/doc");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* the class dictionary holds each unique class once */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_CLASSDICT));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "ELF 64-bit LSB executable");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "");

    /* each file indexes in to the class dictionary */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILECLASS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 0)), 0);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 1)), 0);
    TARPM_ASSERT_EQUAL(json_object_get_int(json_object_array_get_idx(values, 2)), 1);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with langs values */
void
test_add_file_list_tags_langs(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *langs = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a file with several languages */
    file = add_file(files, "/usr/share/man/de/man1/ls.1");
    langs = json_object_new_array();
    json_object_array_add(langs, json_object_new_string("en"));
    json_object_array_add(langs, json_object_new_string("de"));
    json_object_array_add(langs, json_object_new_string("es"));
    json_object_object_add(file, "langs", langs);

    /* a file with a single language */
    file = add_file(files, "/usr/share/man/fr/man1/ls.1");
    langs = json_object_new_array();
    json_object_array_add(langs, json_object_new_string("fr"));
    json_object_object_add(file, "langs", langs);

    /* a file with no languages at all */
    add_file(files, "/usr/bin/ls");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* languages are joined with a "|" and missing ones are empty strings */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILELANGS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 3);
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 0)), "en|de|es");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 1)), "fr");
    TARPM_ASSERT_STRING_EQUAL(json_object_get_string(json_object_array_get_idx(values, 2)), "");

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with color values */
void
test_add_file_list_tags_colors(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *colors = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a 64 bit ELF object */
    file = add_file(files, "/usr/bin/ls");
    colors = json_object_new_array();
    json_object_array_add(colors, json_object_new_string("Elf64"));
    json_object_object_add(file, "colors", colors);

    /* a 32 bit ELF object */
    file = add_file(files, "/usr/bin/cat");
    colors = json_object_new_array();
    json_object_array_add(colors, json_object_new_string("Elf32"));
    json_object_object_add(file, "colors", colors);

    /* an entry carrying both ELF classes */
    file = add_file(files, "/usr/bin/mv");
    colors = json_object_new_array();
    json_object_array_add(colors, json_object_new_string("Elf32"));
    json_object_array_add(colors, json_object_new_string("Elf64"));
    json_object_object_add(file, "colors", colors);

    /* an entry with a color that has no name */
    file = add_file(files, "/usr/bin/cp");
    colors = json_object_new_array();
    json_object_array_add(colors, json_object_new_string("4"));
    json_object_object_add(file, "colors", colors);

    /* an entry with no color at all */
    add_file(files, "/usr/share/man/man1/ls.1");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* names turn back in to bits and missing colors are zero */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILECOLORS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 5);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 1);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 2)), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 3)), 4);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 4)), 0);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with flag values */
void
test_add_file_list_tags_flags(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *flags = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a %config file */
    file = add_file(files, "/etc/ls.conf");
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("config"));
    json_object_object_add(file, "flags", flags);

    /* a %config(noreplace) file */
    file = add_file(files, "/etc/cat.conf");
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("config"));
    json_object_array_add(flags, json_object_new_string("noreplace"));
    json_object_object_add(file, "flags", flags);

    /* a %ghost %config(missingok noreplace) file */
    file = add_file(files, "/etc/mv.conf");
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("config"));
    json_object_array_add(flags, json_object_new_string("missingok"));
    json_object_array_add(flags, json_object_new_string("noreplace"));
    json_object_array_add(flags, json_object_new_string("ghost"));
    json_object_object_add(file, "flags", flags);

    /* an entry with a flag that has no name */
    file = add_file(files, "/usr/bin/cp");
    flags = json_object_new_array();
    json_object_array_add(flags, json_object_new_string("512"));
    json_object_object_add(file, "flags", flags);

    /* an entry with no flags at all */
    add_file(files, "/usr/share/man/man1/ls.1");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /* names turn back in to bits and missing flags are zero */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEFLAGS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 5);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 1);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 17);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 2)), 89);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 3)), 512);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 4)), 0);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with verify flag values */
void
test_add_file_list_tags_verifyflags(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *verifyflags = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();

    /* a file that verifies its digest only */
    file = add_file(files, "/usr/bin/ls");
    verifyflags = json_object_new_array();
    json_object_array_add(verifyflags, json_object_new_string("filedigest"));
    json_object_object_add(file, "verifyflags", verifyflags);

    /* the obsolete spelling maps on to the same bit */
    file = add_file(files, "/usr/bin/cat");
    verifyflags = json_object_new_array();
    json_object_array_add(verifyflags, json_object_new_string("md5"));
    json_object_object_add(file, "verifyflags", verifyflags);

    /* a file that verifies several attributes */
    file = add_file(files, "/usr/bin/mv");
    verifyflags = json_object_new_array();
    json_object_array_add(verifyflags, json_object_new_string("filesize"));
    json_object_array_add(verifyflags, json_object_new_string("user"));
    json_object_array_add(verifyflags, json_object_new_string("group"));
    json_object_array_add(verifyflags, json_object_new_string("mode"));
    json_object_object_add(file, "verifyflags", verifyflags);

    /* a file that verifies everything */
    file = add_file(files, "/usr/bin/cp");
    verifyflags = json_object_new_array();
    json_object_array_add(verifyflags, json_object_new_string("filedigest"));
    json_object_array_add(verifyflags, json_object_new_string("filesize"));
    json_object_array_add(verifyflags, json_object_new_string("linkto"));
    json_object_array_add(verifyflags, json_object_new_string("user"));
    json_object_array_add(verifyflags, json_object_new_string("group"));
    json_object_array_add(verifyflags, json_object_new_string("mtime"));
    json_object_array_add(verifyflags, json_object_new_string("mode"));
    json_object_array_add(verifyflags, json_object_new_string("rdev"));
    json_object_array_add(verifyflags, json_object_new_string("caps"));
    json_object_object_add(file, "verifyflags", verifyflags);

    /* an entry that verifies nothing */
    add_file(files, "/usr/share/man/man1/ls.1");

    add_file_list_tags(tags, files, NULL, NULL, NULL);

    /*
     * Names turn back in to bits the way rpmbuild writes them, which is
     * RPMVERIFY_ALL with the named bits that are absent cleared.  The
     * nine named bits are the low nine, so everything above 0x1ff stays
     * set and an entry with no verify flags at all is 0xfffffe00.
     */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEVERIFYFLAGS));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 5);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 0xfffffe01);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 0xfffffe01);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 2)), 0xfffffe5a);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 3)), 0xffffffff);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 4)), 0xfffffe00);

    json_object_put(tags);
    json_object_put(files);

    return;
}

/*
 * Helper for test_add_file_list_tags_provides() below that builds a
 * single dependency entry carrying one sense flag.
 */
static struct json_object *
add_dependency(struct json_object *deps, const char *name, const char *sense_flag)
{
    struct json_object *entry = NULL;
    struct json_object *sense_flags = NULL;

    entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string(name));

    sense_flags = json_object_new_array();
    json_object_array_add(sense_flags, json_object_new_string(sense_flag));
    json_object_object_add(entry, "sense_flags", sense_flags);

    json_object_array_add(deps, entry);

    return entry;
}

/*
 * Helper for test_add_file_list_tags_provides() below that adds one
 * entry to a file's "provides" array.  The entry names its dependency
 * type and repeats the dependency it points at.
 */
static void
add_provides(struct json_object *provides, const char *type, const char *name, const char *sense_flag)
{
    struct json_object *entry = NULL;

    entry = add_dependency(provides, name, sense_flag);
    json_object_object_add(entry, "type", json_object_new_string(type));

    return;
}

/* Test add_file_list_tags() with provides values */
void
test_add_file_list_tags_provides(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *file = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *deps = NULL;
    struct json_object *provides = NULL;
    struct json_object *values = NULL;

    tags = json_object_new_array();
    files = json_object_new_array();
    dependencies = json_object_new_object();

    /* the package provides one soname */
    deps = json_object_new_array();
    add_dependency(deps, "libfoo.so.1()(64bit)", "find-provides");
    json_object_object_add(dependencies, "provides", deps);

    /* and requires two of them */
    deps = json_object_new_array();
    add_dependency(deps, "libc.so.6()(64bit)", "find-requires");
    add_dependency(deps, "libm.so.6()(64bit)", "find-requires");
    json_object_object_add(dependencies, "requires", deps);

    /*
     * A file that generated all three, with the types interleaved the
     * way the dependency generators leave them.
     */
    file = add_file(files, "/usr/lib64/libfoo.so.1");
    provides = json_object_new_array();
    add_provides(provides, "requires", "libc.so.6()(64bit)", "find-requires");
    add_provides(provides, "provides", "libfoo.so.1()(64bit)", "find-provides");
    add_provides(provides, "requires", "libm.so.6()(64bit)", "find-requires");
    json_object_object_add(file, "provides", provides);

    /* a file that generated nothing */
    add_file(files, "/usr/share/doc/foo/README");

    add_file_list_tags(tags, files, NULL, NULL, dependencies);

    /*
     * Every provides entry becomes one dictionary value holding the
     * type abbreviation and the index of the dependency it matched,
     * and the order of the file's list is kept.
     */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_DEPENDSDICT));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 0x52000000);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 0x50000000);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 2)), 0x52000001);

    /* the first file owns the whole dictionary and the second none of it */
    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDEPENDSX));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 0);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 0);

    values = get_tag_values(tags, rpmTagGetName(RPMTAG_FILEDEPENDSN));
    TARPM_ASSERT_PTR_NOT_NULL(values);
    TARPM_ASSERT_EQUAL(json_object_array_length(values), 2);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 0)), 3);
    TARPM_ASSERT_EQUAL(json_object_get_int64(json_object_array_get_idx(values, 1)), 0);

    json_object_put(tags);
    json_object_put(files);
    json_object_put(dependencies);

    return;
}

/* Test is_file_list_tag() with file list tags */
void
test_is_file_list_tag_file_list_tags(void)
{
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_DIRNAMES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_BASENAMES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_DIRINDEXES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILESIZES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEMODES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEMTIMES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEUSERNAME));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEGROUPNAME));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILERDEVS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEDEVICES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEDIGESTS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILELINKTOS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEINODES));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILECLASS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_CLASSDICT));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILELANGS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILECOLORS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEFLAGS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEVERIFYFLAGS));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_DEPENDSDICT));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEDEPENDSX));
    TARPM_ASSERT_TRUE(is_file_list_tag(RPMTAG_FILEDEPENDSN));

    return;
}

/* Test is_file_list_tag() with tags that are not file list tags */
void
test_is_file_list_tag_other_tags(void)
{
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_NAME));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_VERSION));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_RELEASE));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_ARCH));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_CHANGELOGTIME));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_PROVIDENAME));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_REQUIRENAME));
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_FILECAPS));

    return;
}

CU_pSuite
get_suite(void)
{
    CU_pSuite pSuite = NULL;

    /* add a suite to the registry */
    pSuite = CU_add_suite("files", init_test_files, clean_test_files);

    if (pSuite == NULL) {
        return NULL;
    }

    /* add tests to the suite */
    if (CU_add_test(pSuite, "test generate_files() with NULL", test_generate_files_null) == NULL ||
        CU_add_test(pSuite, "test generate_files() with no file list", test_generate_files_no_file_list) == NULL ||
        CU_add_test(pSuite, "test generate_files() with mismatched lengths", test_generate_files_mismatched_lengths) == NULL ||
        CU_add_test(pSuite, "test generate_files() with a valid file list", test_generate_files_valid) == NULL ||
        CU_add_test(pSuite, "test generate_files() with each file type", test_generate_files_types) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with NULL", test_add_file_list_tags_null) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with empty files", test_add_file_list_tags_empty) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with invalid type", test_add_file_list_tags_invalid_type) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with a file list", test_add_file_list_tags_file_list) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with payload file sizes", test_add_file_list_tags_sizes) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with sizes and no payload", test_add_file_list_tags_sizes_no_payload) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with the installed size", test_add_file_list_tags_total_size) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with each file type", test_add_file_list_tags_types) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with types from the payload", test_add_file_list_tags_type_from_payload) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with a type mismatch", test_add_file_list_tags_type_mismatch) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with values from a type mismatch", test_add_file_list_tags_mismatch_values) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with an rdev from a type mismatch", test_add_file_list_tags_mismatch_rdev) == NULL ||
        CU_add_test(pSuite, "test add_payload_files() with NULL", test_add_payload_files_null) == NULL ||
        CU_add_test(pSuite, "test add_payload_files() with new payload files", test_add_payload_files_new) == NULL ||
        CU_add_test(pSuite, "test add_payload_files() with a source package", test_add_payload_files_source) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with class values", test_add_file_list_tags_class) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with langs values", test_add_file_list_tags_langs) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with color values", test_add_file_list_tags_colors) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with flag values", test_add_file_list_tags_flags) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with verify flag values", test_add_file_list_tags_verifyflags) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with provides values", test_add_file_list_tags_provides) == NULL ||
        CU_add_test(pSuite, "test is_file_list_tag() with file list tags", test_is_file_list_tag_file_list_tags) == NULL ||
        CU_add_test(pSuite, "test is_file_list_tag() with other tags", test_is_file_list_tag_other_tags) == NULL) {
        return NULL;
    }

    return pSuite;
}
