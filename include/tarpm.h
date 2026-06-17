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

/* init.c */
int init_librpm(void);

/* rpm.c */
char *extract_payload(const char *rpm);
Header get_header(const char *pkg);
char *get_rpmtag_str(Header h, rpmTagVal tag);
const char *get_header_arch(Header h);
char *get_nevr(Header h);
char *get_nevra(Header h);

/* strfuncs.c */
bool strprefix(const char *s, const char *prefix);
bool strsuffix(const char *s, const char *suffix);
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
#ifdef _HAVE_REALLOCARRAY
void *xreallocarray(void *p, size_t n, size_t s);
#endif

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
bool valid_header_signature(struct rpmhdr *hdr);
struct json_object *read_header(const int fd);
int create_header(const struct json_object *data, struct rpmhdr **hdr, struct rpmhdrinfo **hdrinfo);

/* joinpath.c */
char *joinpath(const char *path, ...);

/* json.c */
struct json_object *create_json_hdr_entry(const struct rpmhdrentry *hdrentry, const bool signature);
struct json_object *generate_json(const struct rpmhdr *hdr, const struct rpmhdrinfo *svals);
struct json_object *generate_json_entries(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, struct rpmhdrentry *trailer, const bool signature);
struct json_object *read_json_file(const char *input_file);
int write_json_file(struct json_object *data, const char *output_dir, const char *output_file);
void free_json(struct json_object *data);

/* tags.c */
const char *strtagtype(rpmTagType type);
rpmTagType tag_type(struct json_object *tag);
const char *sig_tag_name(uint32_t tag);
const char *get_tag_value(const struct json_object *tags, const char *name);

/* read.c */
struct rpmhdrinfo *compute_hdrinfo(const struct rpmhdr *hdr, const bool signature);
struct rpmhdr *read_header_signature(const int fd);
uint32_t *read_header_entries(const int fd, const struct rpmhdr *hdr, const uint32_t hlen);
struct rpmhdrentry *read_header_trailer(const struct rpmhdrentry *entry, const uint8_t *datastart);

/* entry.c */
void add_entry_value(struct json_object *arrayentry, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count);

#endif /* _TARPM_TARPM_H */
