/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TARPM_TARPM_H
#define _TARPM_TARPM_H

#include <stdbool.h>
#include <sys/stat.h>
#include <rpm/header.h>

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

/* header.c */
int extract_lead(const char *rpm, const char *output_dir);
int extract_header(Header h, const char *output_dir);

/* joinpath.c */
char *joinpath(const char *path, ...);

#endif /* _TARPM_TARPM_H */
