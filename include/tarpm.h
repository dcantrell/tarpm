/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_TARPM_H
#define _TARPM_TARPM_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <rpm/header.h>
#include <json.h>

#include "constants.h"
#include "i18n.h"
#include "helpers.h"
#include "types.h"

/* abspath.c */
char *abspath(const char *path);
char *dir_name(const char *path);
char *base_name(const char *path);

/* rpm.c */
char *convert_payload(const char *rpm);
Header get_header(const char *pkg);
char *get_rpmtag_str(Header h, rpmTagVal tag);
const char *get_header_arch(Header h);
char *get_nevr(Header h);
char *get_nevra(Header h);

/* strfuncs.c */
char *strappend(char *dest, ...);
str_list_t *strsplit(const char *s, const char *delim);

/* listfuncs.c */
void list_free(str_list_t *list, list_entry_data_free_func free_func);
str_list_t *list_add(str_list_t *list, const char *s);
char *list_to_string(const str_list_t *list, const char *delimiter);
void uint32_list_free(uint32_list_t *list);
uint32_list_t *uint32_list_add(uint32_list_t *list, const uint32_t value);
uint32_t str_list_len(const str_list_t *list);
uint32_t uint32_list_len(const uint32_list_t *list);
const char *str_list_nth(const str_list_t *list, uint32_t index);
bool uint32_list_nth(const uint32_list_t *list, uint32_t index, uint32_t *value);
str_entry_t *first_str(str_list_t *list);
uint32_entry_t *first_uint32(uint32_list_t *list);
str_entry_t *next_str(str_entry_t *entry);
uint32_entry_t *next_uint32(uint32_entry_t *entry);

/* xalloc.c */
void *xcalloc(size_t n, size_t s);
void *xalloc(size_t s);
void *xrealloc(void *p, size_t s);

/* mkdirp.c */
int mkdirp(const char *path, mode_t mode);

/* unpack.c */
int unpack_archive(const char *archive, const char *dest, const bool verbose);

/* lead.c */
struct json_object *read_lead(const int fd);
struct rpmlead *create_lead(struct json_object *header);

/* format.c */
/*
 * The RPM format we write the package for, which is RPM_FORMAT_V4 or
 * RPM_FORMAT_V6.  We set this from the command line before we create
 * an RPM.
 */
extern int rpmformat;

int apply_rpmformat(struct json_object *header, const int format);
int update_payload_tags(struct json_object *header, const int payloadfd, const int format);

/* signature.c */
struct json_object *read_signature(const int fd);
struct json_object *make_signature(const int format);

/* header.c */
bool valid_header(struct rpmhdr *hdr);
struct json_object *read_header(const int fd, const char *dest_dir);
int create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo, const char *payload_dir, const char *tagfile_dir, bool is_signature);
bool has_trailer(const uint32_t nentries, const struct rpmhdrentry *estart);
int get_trailer_data(const struct json_object *data, uint8_t **trailer_data, size_t *trailer_size);
void fix_trailer_offset(uint8_t *trailer_data, const size_t trailer_size, const uint32_t nentries);

/* changelog.c */
struct json_object *generate_changelog(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo);
int add_changelog_tags(struct json_object *tags, struct json_object *changelog);
bool is_changelog_tag(rpmTagVal tag);

/* deps.c */
struct json_object *generate_dependencies(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo);
void add_dependency_tags(struct json_object *tags, struct json_object *dependencies);
bool is_dependency_tag(rpmTagVal tag);
const char *dependency_type_key(const char abbrev);
char dependency_type_abbrev(const char *key);
int dependency_index(struct json_object *dependencies, const char *key, struct json_object *entry);

/* files.c */
/*
 * The PAYLOAD_OVERRIDE_* bits for the file metadata we take from the
 * payload tree instead of from header.json.  We set this from the
 * command line before we create an RPM.
 */
extern uint32_t payload_overrides;

struct json_object *generate_files(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct json_object *dependencies);
void add_payload_files(struct json_object *tags, struct json_object *files, const char *payload_dir);
void add_file_list_tags(struct json_object *tags, struct json_object *files, const char *payload_dir, struct json_object *dependencies);
bool is_file_list_tag(rpmTagVal tag);

/* class.c */
bool wanted_class(const char *name);
const char *skipped_class(const char *path);
char *file_class(const char *path, const char *file_path, const struct stat *sb);
void close_magic(void);

/* joinpath.c */
char *joinpath(const char *path, ...);

/* json.c */
struct json_object *create_json_entry(const struct rpmhdrentry *hdrentry, const bool signature);
struct json_object *generate_json(const struct rpmhdr *hdr, const struct rpmhdrinfo *svals);
struct json_object *generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const char *dest_dir, const bool signature);
struct json_object *read_json_file(const char *input_file);
int write_json_file(struct json_object *data, const char *path);

/* tags.c */
const char *strtagtype(rpmTagType type);
char *strdigestalgo(uint32_t algo);
uint32_t digest_algo(const char *name);
char *strbuildtime(uint32_t buildtime);
uint32_t buildtime_value(const char *timestamp);
rpmTagType tag_type(struct json_object *tag);
const char *sig_tag_name(uint32_t tag);
bool is_rpmsign_tag(uint32_t tag);
rpmTagVal get_tag_number(struct json_object *entry, bool signature);
const char *get_tag_value(const struct json_object *tags, const char *name);
int set_tag_value(struct json_object *tags, const char *name, const char *new_value);

/* read.c */
struct rpmhdrinfo *mkhdrinfo(const struct rpmhdr *hdr, const bool signature);
struct rpmhdr *read_header_signature(const int fd);
uint32_t *read_header_entries(const int fd, const struct rpmhdr *hdr, const uint32_t hlen);
struct rpmhdrentry *read_header_trailer(const struct rpmhdr *hdr, const struct rpmhdrentry *estart, const uint8_t *datastart);

/* reset.c */
void reset_librpm(void);

/* entry.c */
bool is_file_tag(rpmTagVal tag);
char *get_tag_filename(rpmTagVal tag, const char *ending);
void add_entry_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count, const char *dest_dir, const bool signature);

/* xread.c */
bool xread(int fd, void *buf, size_t count);

/* extract.c */
int extract_rpm(const char *filename, const char *cwd, const char *output_dir, const struct json_paths *paths, const bool verbose);

/* create.c */
int create_rpm(const char *filename, const char *cwd, const char *input_dir, const struct json_paths *paths);

/* list.c */
void list_rpm(const char *rpm);

/* digest.c */
unsigned char *mksigdigest(const int type, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct json_object *data, const int fd);
char *payload_digest(const int type, const int payloadfd, uint64_t *size);
char *archive_digest(const int type, const int payloadfd, uint64_t *size);
char *nul_digest(const int type);

/* strmode.c */
void strmode(mode_t mode, char *p);

/* readfile.c */
void *read_file_bytes(const char *path, off_t *len);

#endif /* _TARPM_TARPM_H */
