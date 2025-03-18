/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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

/* init.c */
int init_librpm(void);

/* rpm.c */
char *extract_rpm_payload(const char *rpm);
Header get_rpm_header(const char *pkg);
char *get_rpmtag_str(Header h, rpmTagVal tag);
const char *get_rpm_header_arch(Header h);
char *get_nevr(Header h);
char *get_nevra(Header h);

/* strfuncs.c */
bool strprefix(const char *s, const char *prefix);
bool strsuffix(const char *s, const char *suffix);
char *strappend(char *dest, ...);

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
int unpack_archive(const char *archive, const char *dest, const bool force, const bool verbose);

/* lead.c */
int extract_lead(const int fd, const char *output_dir);

/* signature.c */
int extract_signature(const int fd, const char *output_dir);

/* header.c */
int extract_header(const int fd, const char *output_dir);

/* joinpath.c */
char *joinpath(const char *path, ...);

/* json.c */
int write_json_file(struct json_object *data, const char *output_dir, const char *output_file);
void free_json(struct json_object *data);

/* tags.c */
const char *tag_type(rpmTagType type);
const char *signature_tag_name(rpmSigTag tag);
const char *tag_name(rpmTag tag);

/* read.c */
struct rpmsigvalues *compute_sigvalues(const struct rpmsignature *sig, const bool signature);
struct rpmsignature *read_header_signature(const int fd);
uint32_t *read_header_entries(const int fd, const struct rpmsignature *sig, const uint32_t hlen);
struct rpmidxentry *read_header_trailer(const struct rpmidxentry *entry, const uint8_t *datastart);

/* entry.c */
void add_entry_value(struct json_object *arrayentry, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count);

/* write.c */
struct json_object *generate_json(const struct rpmsignature *sig, const struct rpmsigvalues *svals);
struct json_object *generate_json_entries(const struct rpmsignature *sig, const struct rpmsigvalues *svals, struct rpmidxentry *entry, const bool signature);

#endif /* _TARPM_TARPM_H */
