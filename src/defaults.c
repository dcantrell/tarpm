/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <err.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmpgp.h>
#include <rpm/rpmbase64.h>
#include <json.h>

#include "tarpm.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

/*
 * Add a string tag if the header does not already carry it.  Every
 * default goes through here, which is what keeps the pass additive.
 */
static void
add_default_tag(struct json_object *tags, const rpmTagVal tag, const rpmTagType type, const char *value)
{
    const char *name = NULL;
    struct json_object *entry = NULL;

    name = rpmTagGetName(tag);

    if (value == NULL || has_tag(tags, name)) {
        return;
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(name));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(type)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
    json_object_array_add(tags, entry);

    return;
}

/*
 * Returns true if the tree already describes the files of the
 * package.  The digests in a tree like that were made with the
 * algorithm the package was built with and rpm reads a missing
 * FILEDIGESTALGO as md5, so we leave the tag alone.
 */
static bool
has_file_list(struct json_object *header, struct json_object *tags)
{
    struct json_object *files = NULL;

    if (has_tag(tags, rpmTagGetName(RPMTAG_FILEDIGESTS))) {
        return true;
    }

    if (!json_object_object_get_ex(header, RPM_FILES_DESC, &files)) {
        return false;
    }

    if (json_object_get_type(files) != json_type_array) {
        return false;
    }

    return (json_object_array_length(files) > 0);
}

/*
 * Add the region trailer that marks the header immutable.  rpm reads
 * a header with no region as a v3 package and logs that v3 is
 * deprecated.  The trailer is one index entry pointing back at the
 * start of the index and write_header() works the offset out again.
 * Returns non-zero on failure.
 */
static int
add_region_trailer(struct json_object *tags)
{
    char *value = NULL;
    const char *name = NULL;
    struct rpmhdrentry region;
    struct json_object *entry = NULL;
    struct json_object *trailer = NULL;

    name = rpmTagGetName(RPMTAG_HEADERIMMUTABLE);

    if (has_tag(tags, name)) {
        return 0;
    }

    region.tag = htonl(RPMTAG_HEADERIMMUTABLE);
    region.type = htonl(RPM_BIN_TYPE);
    region.offset = 0;
    region.count = htonl(sizeof(region));

    value = rpmBase64Encode(&region, sizeof(region), -1);

    if (value == NULL) {
        warnx("rpmBase64Encode");
        return -1;
    }

    trailer = json_object_new_object();
    json_object_object_add(trailer, RPM_ENTRY_TAG_DESC, json_object_new_string(name));
    json_object_object_add(trailer, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_BIN_TYPE)));

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(name));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_BIN_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_TRAILER_DESC, trailer);
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(value));
    json_object_array_add(tags, entry);

    free(value);

    return 0;
}

/*
 * Build the EVR string the package provides itself under.  rpm leaves
 * the epoch out when it is zero or missing.
 * NOTE: Caller must free this result.
 */
static char *
package_evr(struct json_object *tags)
{
    char *evr = NULL;
    const char *e = NULL;
    const char *v = NULL;
    const char *r = NULL;

    e = get_tag_value(tags, rpmTagGetName(RPMTAG_EPOCH));
    v = get_tag_value(tags, rpmTagGetName(RPMTAG_VERSION));
    r = get_tag_value(tags, rpmTagGetName(RPMTAG_RELEASE));

    if (e != NULL && strcmp(e, "0")) {
        xasprintf(&evr, "%s:%s-%s", e, v, r);
    } else {
        xasprintf(&evr, "%s-%s", v, r);
    }

    return evr;
}

/*
 * Add the provide every package carries for itself, which is its name
 * at its own EVR.  addTE() in lib/rpmte.cc builds this one itself, so
 * an install does not need it, but rpmbuild writes it and
 * rpm -q --provides expects it.  A header that already provides the
 * name keeps what it has.
 */
static void
add_self_provide(struct json_object *header, struct json_object *tags)
{
    size_t i = 0;
    char *evr = NULL;
    const char *name = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *provides = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    name = get_tag_value(tags, rpmTagGetName(RPMTAG_NAME));

    if (!json_object_object_get_ex(header, RPM_DEPENDENCIES_DESC, &dependencies)) {
        dependencies = json_object_new_object();
        json_object_object_add(header, RPM_DEPENDENCIES_DESC, dependencies);
    }

    if (!json_object_object_get_ex(dependencies, RPM_DEPENDENCY_PROVIDES_KEY, &provides)) {
        provides = json_object_new_array();
        json_object_object_add(dependencies, RPM_DEPENDENCY_PROVIDES_KEY, provides);
    }

    /* the header may already provide the name */
    for (i = 0; i < json_object_array_length(provides); i++) {
        entry = json_object_array_get_idx(provides, i);

        if (json_object_object_get_ex(entry, RPM_DEPENDENCY_NAME_DESC, &value) && !strcmp(name, json_object_get_string(value))) {
            return;
        }
    }

    evr = package_evr(tags);

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_DEPENDENCY_NAME_DESC, json_object_new_string(name));
    json_object_object_add(entry, RPM_DEPENDENCY_COMPARISON_DESC, json_object_new_string(COMPARISON_EQ));
    json_object_object_add(entry, RPM_DEPENDENCY_VERSION_DESC, json_object_new_string(evr));
    json_object_array_add(provides, entry);

    free(evr);

    return;
}

/*
 * Fill in the main header tags the tree did not name.  Name, Version
 * and Release have no value we can work out, so a header without them
 * is an error.  Everything else here is additive, which is what lets
 * an extracted tree go back out unchanged.  Returns non-zero on
 * failure.
 */
int
apply_header_defaults(struct json_object *header, const int format)
{
    char *buf = NULL;
    const char *name = NULL;
    const char *arch = NULL;
    bool source_package = false;
    struct json_object *tags = NULL;
    struct utsname uts;
    char hostname[HOST_NAME_MAX + 1];

    if (header == NULL) {
        return -1;
    }

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        tags = json_object_new_array();
        json_object_object_add(header, RPM_ENTRY_TAGS_DESC, tags);
    }

    if (json_object_get_type(tags) != json_type_array) {
        warnx(_("*** apply_header_defaults: tags must be an array"));
        return -1;
    }

    name = get_tag_value(tags, rpmTagGetName(RPMTAG_NAME));

    if (name == NULL || get_tag_value(tags, rpmTagGetName(RPMTAG_VERSION)) == NULL || get_tag_value(tags, rpmTagGetName(RPMTAG_RELEASE)) == NULL) {
        warnx(_("*** missing %s, %s, or %s in header data"), rpmTagGetName(RPMTAG_NAME), rpmTagGetName(RPMTAG_VERSION), rpmTagGetName(RPMTAG_RELEASE));
        return -1;
    }

    /* inventing a license string is worse than an empty field */
    if (!has_tag(tags, rpmTagGetName(RPMTAG_LICENSE))) {
        warnx(_("*** missing %s in header data, continuing without it"), rpmTagGetName(RPMTAG_LICENSE));
    }

    source_package = (get_tag_value(tags, rpmTagGetName(RPMTAG_SOURCEPACKAGE)) != NULL);

    /* addTE() in lib/rpmte.cc turns away a package with no os or arch */
    add_default_tag(tags, RPMTAG_OS, RPM_STRING_TYPE, RPM_DEFAULT_OS);

    if (source_package) {
        arch = SRPM_ARCH_NAME;
    } else if (uname(&uts) == 0) {
        arch = uts.machine;
    } else {
        warn("uname");
    }

    add_default_tag(tags, RPMTAG_ARCH, RPM_STRING_TYPE, arch);

    /* headerCheckPayloadFormat() in lib/depends.cc takes cpio and nothing else */
    add_default_tag(tags, RPMTAG_PAYLOADFORMAT, RPM_STRING_TYPE, RPM_DEFAULT_PAYLOADFORMAT);

    /* apply_rpmformat() moves a v6 package to zstd, so start it there */
    if (format == RPM_FORMAT_V6) {
        add_default_tag(tags, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE, RPMFORMAT_V6_COMPRESSOR);
        add_default_tag(tags, RPMTAG_PAYLOADFLAGS, RPM_STRING_TYPE, RPMFORMAT_V6_COMPRESSOR_LEVEL);
    } else {
        add_default_tag(tags, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE, RPM_DEFAULT_PAYLOADCOMPRESSOR);
        add_default_tag(tags, RPMTAG_PAYLOADFLAGS, RPM_STRING_TYPE, RPM_DEFAULT_PAYLOADFLAGS);
    }

    /* applyRetrofits() in lib/package.cc writes this on read, so match it */
    if (!source_package) {
        add_default_tag(tags, RPMTAG_SOURCERPM, RPM_STRING_TYPE, RPM_DEFAULT_SOURCERPM);
    }

    /* the digests we are about to make come out of a fresh scan */
    if (!has_file_list(header, tags)) {
        buf = strdigestalgo(PGPHASHALGO_SHA256);
        add_default_tag(tags, RPMTAG_FILEDIGESTALGO, RPM_STRING_TYPE, buf);
        free(buf);
    }

    buf = strbuildtime((uint32_t) time(NULL));
    add_default_tag(tags, RPMTAG_BUILDTIME, RPM_STRING_TYPE, buf);
    free(buf);

    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        add_default_tag(tags, RPMTAG_BUILDHOST, RPM_STRING_TYPE, hostname);
    } else {
        warn("gethostname");
    }

    /* queries look wrong with neither of these, so fall back to the name */
    add_default_tag(tags, RPMTAG_SUMMARY, RPM_I18NSTRING_TYPE, name);
    add_default_tag(tags, RPMTAG_DESCRIPTION, RPM_I18NSTRING_TYPE, name);

    add_self_provide(header, tags);

    return add_region_trailer(tags);
}
