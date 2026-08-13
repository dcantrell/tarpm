/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef _TARPM_TARPM_H
#define _TARPM_TARPM_H

#include <stdbool.h>
#include <sys/stat.h>
#include <rpm/header.h>
#include <json.h>

#include "constants.h"
#include "i18n.h"
#include "helpers.h"
#include "types.h"

/* abspath.c */
char *abspath(const char *path);

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

/* xalloc.c */
void *xcalloc(size_t n, size_t s);
void *xalloc(size_t s);
void *xrealloc(void *p, size_t s);

/* mkdirp.c */
int mkdirp(const char *path, mode_t mode);

/* unpack.c */
int unpack_archive(const char *archive, const char *dest, const bool list, const bool verbose);

/* lead.c */
struct json_object *read_lead(const int fd);
struct rpmlead *create_lead(struct json_object *header);

/* signature.c */
struct json_object *read_signature(const int fd);

/* header.c */
bool valid_header(struct rpmhdr *hdr);
struct json_object *read_header(const int fd, const char *dest_dir);
int create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo);
bool has_trailer(const uint32_t nentries, const struct rpmhdrentry *estart);
int get_trailer_data(const struct json_object *data, uint8_t **trailer_data, size_t *trailer_size);

/* joinpath.c */
char *joinpath(const char *path, ...);

/* json.c */
struct json_object *create_json_entry(const struct rpmhdrentry *hdrentry, const bool signature);
struct json_object *generate_json(const struct rpmhdr *hdr, const struct rpmhdrinfo *svals);
struct json_object *generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const char *dest_dir, const bool signature);
struct json_object *read_json_file(const char *input_file);
int write_json_file(struct json_object *data, const char *output_dir, const char *output_file);

/* tags.c */
const char *strtagtype(rpmTagType type);
rpmTagType tag_type(struct json_object *tag);
const char *sig_tag_name(uint32_t tag);
rpmTagVal get_tag_number(struct json_object *entry);
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
void add_entry_value(struct json_object *arrayentry, rpmTagVal tag, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count, const char *dest_dir);

/* xread.c */
bool xread(int fd, void *buf, size_t count);

/* extract.c */
void extract_rpm(const char *filename, const char *cwd, const char *output_dir, const bool verbose);

/* create.c */
void create_rpm(const char *filename, const char *cwd, const char *input_dir);

/* list.c */
void list_rpm(const char *rpm);

/* digest.c */
unsigned char *mksigdigest(const int type, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct json_object *data, const int fd);

/* strmode.c */
void strmode(mode_t mode, char *p);

/* readfile.c */
void *read_file_bytes(const char *path, off_t *len);
char *read_file(const char *path);

#endif /* _TARPM_TARPM_H */
