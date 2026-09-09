/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <sys/stat.h>
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
 */
#define DIRNAMES_OFFSET    0
#define BASENAMES_OFFSET   10
#define DIRINDEXES_OFFSET  17
#define FILESIZES_OFFSET   25
#define FILEMODES_OFFSET   33
#define FILELANGS_OFFSET   37
#define FILECOLORS_OFFSET  44
#define FILEFLAGS_OFFSET   52
#define DATA_SIZE          60
#define NUM_ENTRIES        8

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

    set_entry(&entries[0], RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE, DIRNAMES_OFFSET, 1);
    set_entry(&entries[1], RPMTAG_BASENAMES, RPM_STRING_ARRAY_TYPE, BASENAMES_OFFSET, 2);
    set_entry(&entries[2], RPMTAG_DIRINDEXES, RPM_INT32_TYPE, DIRINDEXES_OFFSET, 2);
    set_entry(&entries[3], RPMTAG_FILESIZES, RPM_INT32_TYPE, FILESIZES_OFFSET, 2);
    set_entry(&entries[4], RPMTAG_FILEMODES, RPM_INT16_TYPE, FILEMODES_OFFSET, 2);
    set_entry(&entries[5], RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE, FILELANGS_OFFSET, 2);
    set_entry(&entries[6], RPMTAG_FILECOLORS, RPM_INT32_TYPE, FILECOLORS_OFFSET, 2);
    set_entry(&entries[7], RPMTAG_FILEFLAGS, RPM_INT32_TYPE, FILEFLAGS_OFFSET, 2);

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

    TARPM_ASSERT_PTR_NULL(generate_files(NULL, NULL));
    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, NULL));
    TARPM_ASSERT_PTR_NULL(generate_files(NULL, &hdrinfo));

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

    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, &hdrinfo));

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

    TARPM_ASSERT_PTR_NULL(generate_files(&hdr, &hdrinfo));

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

    build_header(&hdr, &hdrinfo, entries, data);

    files = generate_files(&hdr, &hdrinfo);
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

    json_object_put(files);

    return;
}

/* Test add_file_list_tags() with NULL inputs */
void
test_add_file_list_tags_null(void)
{
    struct json_object *tags = NULL;
    struct json_object *files = NULL;

    /* all NULL should not crash */
    add_file_list_tags(NULL, NULL, NULL, NULL);

    /* NULL files should add no tags */
    tags = json_object_new_array();
    add_file_list_tags(tags, NULL, NULL, NULL);
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 0);

    /* NULL tags should not crash */
    files = json_object_new_array();
    add_file_list_tags(NULL, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

    /* all eighteen file list tags should be present */
    TARPM_ASSERT_EQUAL(json_object_array_length(tags), 18);

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

    json_object_put(tags);
    json_object_put(files);

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

    add_file_list_tags(tags, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

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

    add_file_list_tags(tags, files, NULL, NULL);

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
    TARPM_ASSERT_FALSE(is_file_list_tag(RPMTAG_FILEVERIFYFLAGS));

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
        CU_add_test(pSuite, "test add_file_list_tags() with NULL", test_add_file_list_tags_null) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with empty files", test_add_file_list_tags_empty) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with invalid type", test_add_file_list_tags_invalid_type) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with a file list", test_add_file_list_tags_file_list) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with class values", test_add_file_list_tags_class) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with langs values", test_add_file_list_tags_langs) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with color values", test_add_file_list_tags_colors) == NULL ||
        CU_add_test(pSuite, "test add_file_list_tags() with flag values", test_add_file_list_tags_flags) == NULL ||
        CU_add_test(pSuite, "test is_file_list_tag() with file list tags", test_is_file_list_tag_file_list_tags) == NULL ||
        CU_add_test(pSuite, "test is_file_list_tag() with other tags", test_is_file_list_tag_other_tags) == NULL) {
        return NULL;
    }

    return pSuite;
}
