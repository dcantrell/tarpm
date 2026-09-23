/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <errno.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <rpm/rpmds.h>
#include <json.h>

#include "tarpm.h"

/*
 * The single character abbreviations in the last column are the ones
 * the depends dictionary uses to name a dependency type.  They come
 * from depTypes[] in lib/rpmds.cc in the rpm source.
 */
static const struct dep_type dep_types[] = {
    { "provides",    RPMTAG_PROVIDENAME,    RPMTAG_PROVIDEFLAGS,    RPMTAG_PROVIDEVERSION,    'P' },
    { "requires",    RPMTAG_REQUIRENAME,    RPMTAG_REQUIREFLAGS,    RPMTAG_REQUIREVERSION,    'R' },
    { "conflicts",   RPMTAG_CONFLICTNAME,   RPMTAG_CONFLICTFLAGS,   RPMTAG_CONFLICTVERSION,   'C' },
    { "obsoletes",   RPMTAG_OBSOLETENAME,   RPMTAG_OBSOLETEFLAGS,   RPMTAG_OBSOLETEVERSION,   'O' },
    { "recommends",  RPMTAG_RECOMMENDNAME,  RPMTAG_RECOMMENDFLAGS,  RPMTAG_RECOMMENDVERSION,  'r' },
    { "suggests",    RPMTAG_SUGGESTNAME,    RPMTAG_SUGGESTFLAGS,    RPMTAG_SUGGESTVERSION,    's' },
    { "supplements", RPMTAG_SUPPLEMENTNAME, RPMTAG_SUPPLEMENTFLAGS, RPMTAG_SUPPLEMENTVERSION, 'S' },
    { "enhances",    RPMTAG_ENHANCENAME,    RPMTAG_ENHANCEFLAGS,    RPMTAG_ENHANCEVERSION,    'e' },
    { NULL, 0, 0, 0, '\0' }
};

/*
 * Convert comparison flags (LESS, GREATER, EQUAL) to symbolic representation.
 * Returns allocated string. Caller must free.
 */
static char *
comparison_to_str(uint32_t flags)
{
    char *r = NULL;

    /* Extract only comparison flags */
    flags = flags & (RPMSENSE_LESS | RPMSENSE_GREATER | RPMSENSE_EQUAL);

    if (flags == 0) {
        return NULL;
    }

    /* Handle combinations */
    if ((flags & RPMSENSE_LESS) && (flags & RPMSENSE_EQUAL)) {
        r = strdup(COMPARISON_LE);
    } else if ((flags & RPMSENSE_GREATER) && (flags & RPMSENSE_EQUAL)) {
        r = strdup(COMPARISON_GE);
    } else if (flags & RPMSENSE_LESS) {
        r = strdup(COMPARISON_LT);
    } else if (flags & RPMSENSE_GREATER) {
        r = strdup(COMPARISON_GT);
    } else if (flags & RPMSENSE_EQUAL) {
        r = strdup(COMPARISON_EQ);
    }

    return r;
}

/*
 * Convert symbolic comparison string to RPMSENSE flags.
 * Returns the comparison flags.
 */
static uint32_t
str_to_comparison(const char *s)
{
    uint32_t flags = 0;

    if (s == NULL || *s == '\0') {
        return 0;
    }

    if (!strcmp(s, COMPARISON_LE)) {
        flags = RPMSENSE_LESS | RPMSENSE_EQUAL;
    } else if (!strcmp(s, COMPARISON_GE)) {
        flags = RPMSENSE_GREATER | RPMSENSE_EQUAL;
    } else if (!strcmp(s, COMPARISON_LT)) {
        flags = RPMSENSE_LESS;
    } else if (!strcmp(s, COMPARISON_GT)) {
        flags = RPMSENSE_GREATER;
    } else if (!strcmp(s, COMPARISON_EQ)) {
        flags = RPMSENSE_EQUAL;
    }

    return flags;
}

/*
 * Convert RPMSENSE flags to JSON array of flag strings.
 * Excludes comparison flags (LESS, GREATER, EQUAL).
 * Returns JSON array object or NULL if no flags.
 */
static struct json_object *
generate_sense_flags(uint32_t flags)
{
    struct json_object *sense_flags = NULL;

    /* Exclude comparison flags - they go in the comparison field */
    flags = flags & ~(RPMSENSE_LESS | RPMSENSE_GREATER | RPMSENSE_EQUAL);

    if (flags == 0) {
        return NULL;
    }

    sense_flags = json_object_new_array();

    /* Handle special flags - order matches deptypeFormat() in rpm */
    if (flags & RPMSENSE_SCRIPT_PRE) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_PRE));
    }

    if (flags & RPMSENSE_SCRIPT_POST) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_POST));
    }

    if (flags & RPMSENSE_SCRIPT_PREUN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_PREUN));
    }

    if (flags & RPMSENSE_SCRIPT_POSTUN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_POSTUN));
    }

    if (flags & RPMSENSE_SCRIPT_VERIFY) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_VERIFY));
    }

    if (flags & RPMSENSE_INTERP) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_INTERP));
    }

    if (flags & RPMSENSE_RPMLIB) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_RPMLIB));
    }

    /* Special handling: FIND_REQUIRES and FIND_PROVIDES get separate entries */
    if (flags & RPMSENSE_FIND_REQUIRES) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_FIND_REQUIRES));
    }

    if (flags & RPMSENSE_FIND_PROVIDES) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_FIND_PROVIDES));
    }

    if (flags & RPMSENSE_PREREQ) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_PREREQ));
    }

    if (flags & RPMSENSE_PRETRANS) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_PRETRANS));
    }

    if (flags & RPMSENSE_POSTTRANS) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_POSTTRANS));
    }

#ifdef _HAS_UNTRANS_TAG
    if (flags & RPMSENSE_PREUNTRANS) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_PREUNTRANS));
    }

    if (flags & RPMSENSE_POSTUNTRANS) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_POSTUNTRANS));
    }
#endif

    if (flags & RPMSENSE_CONFIG) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_CONFIG));
    }

    if (flags & RPMSENSE_MISSINGOK) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_MISSINGOK));
    }

#ifdef _HAS_META_TAG
    if (flags & RPMSENSE_META) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_META));
    }
#endif

    if (flags & RPMSENSE_TRIGGERIN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_TRIGGERIN));
    }

    if (flags & RPMSENSE_TRIGGERUN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_TRIGGERUN));
    }

    if (flags & RPMSENSE_TRIGGERPOSTUN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_TRIGGERPOSTUN));
    }

    if (flags & RPMSENSE_TRIGGERPREIN) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_TRIGGERPREIN));
    }

    if (flags & RPMSENSE_KEYRING) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_KEYRING));
    }

    return sense_flags;
}

/*
 * Convert JSON array of flag strings back to RPMSENSE flags.
 * Returns the flags value.
 */
static uint32_t
read_sense_flags(struct json_object *sense_flags)
{
    uint32_t flags = 0;
    size_t i = 0;
    const char *s = NULL;
    struct json_object *entry = NULL;

    if (sense_flags == NULL || json_object_get_type(sense_flags) != json_type_array) {
        return 0;
    }

    for (i = 0; i < json_object_array_length(sense_flags); i++) {
        entry = json_object_array_get_idx(sense_flags, i);

        if (entry == NULL) {
            continue;
        }

        s = json_object_get_string(entry);

        if (s == NULL) {
            continue;
        }

        if (!strcmp(s, SENSE_FLAG_PRE)) {
            flags |= RPMSENSE_SCRIPT_PRE;
        } else if (!strcmp(s, SENSE_FLAG_POST)) {
            flags |= RPMSENSE_SCRIPT_POST;
        } else if (!strcmp(s, SENSE_FLAG_PREUN)) {
            flags |= RPMSENSE_SCRIPT_PREUN;
        } else if (!strcmp(s, SENSE_FLAG_POSTUN)) {
            flags |= RPMSENSE_SCRIPT_POSTUN;
        } else if (!strcmp(s, SENSE_FLAG_VERIFY)) {
            flags |= RPMSENSE_SCRIPT_VERIFY;
        } else if (!strcmp(s, SENSE_FLAG_INTERP)) {
            flags |= RPMSENSE_INTERP;
        } else if (!strcmp(s, SENSE_FLAG_RPMLIB)) {
            flags |= RPMSENSE_RPMLIB;
        } else if (!strcmp(s, SENSE_FLAG_FIND_REQUIRES)) {
            flags |= RPMSENSE_FIND_REQUIRES;
        } else if (!strcmp(s, SENSE_FLAG_FIND_PROVIDES)) {
            flags |= RPMSENSE_FIND_PROVIDES;
        } else if (!strcmp(s, SENSE_FLAG_PREREQ)) {
            flags |= RPMSENSE_PREREQ;
        } else if (!strcmp(s, SENSE_FLAG_PRETRANS)) {
            flags |= RPMSENSE_PRETRANS;
        } else if (!strcmp(s, SENSE_FLAG_POSTTRANS)) {
            flags |= RPMSENSE_POSTTRANS;
#ifdef _HAS_UNTRANS_TAG
        } else if (!strcmp(s, SENSE_FLAG_PREUNTRANS)) {
            flags |= RPMSENSE_PREUNTRANS;
        } else if (!strcmp(s, SENSE_FLAG_POSTUNTRANS)) {
            flags |= RPMSENSE_POSTUNTRANS;
#endif
        } else if (!strcmp(s, SENSE_FLAG_CONFIG)) {
            flags |= RPMSENSE_CONFIG;
        } else if (!strcmp(s, SENSE_FLAG_MISSINGOK)) {
            flags |= RPMSENSE_MISSINGOK;
#ifdef _HAS_META_TAG
        } else if (!strcmp(s, SENSE_FLAG_META)) {
            flags |= RPMSENSE_META;
#endif
        } else if (!strcmp(s, SENSE_FLAG_TRIGGERIN)) {
            flags |= RPMSENSE_TRIGGERIN;
        } else if (!strcmp(s, SENSE_FLAG_TRIGGERUN)) {
            flags |= RPMSENSE_TRIGGERUN;
        } else if (!strcmp(s, SENSE_FLAG_TRIGGERPOSTUN)) {
            flags |= RPMSENSE_TRIGGERPOSTUN;
        } else if (!strcmp(s, SENSE_FLAG_TRIGGERPREIN)) {
            flags |= RPMSENSE_TRIGGERPREIN;
        } else if (!strcmp(s, SENSE_FLAG_KEYRING)) {
            flags |= RPMSENSE_KEYRING;
        }
    }

    return flags;
}

/*
 * Return the string value of a key in a dependency entry.  Keys that
 * carry no value are left out of the entry entirely, so a missing key
 * reads back as an empty string the same way it went in.
 */
static const char *
dependency_string(struct json_object *entry, const char *key)
{
    struct json_object *obj = NULL;
    const char *s = NULL;

    if (entry == NULL || !json_object_object_get_ex(entry, key, &obj)) {
        return "";
    }

    s = json_object_get_string(obj);

    if (s == NULL) {
        return "";
    }

    return s;
}

/*
 * Combine the comparison operator and the sense flags of a dependency
 * entry in to the single RPMSENSE flag word the header carries.
 */
static uint32_t
dependency_entry_flags(struct json_object *entry)
{
    uint32_t flags = 0;
    struct json_object *obj = NULL;

    if (entry != NULL) {
        if (json_object_object_get_ex(entry, RPM_DEPENDENCY_COMPARISON_DESC, &obj)) {
            flags |= str_to_comparison(json_object_get_string(obj));
        }

        if (json_object_object_get_ex(entry, RPM_SENSE_FLAGS_DESC, &obj)) {
            flags |= read_sense_flags(obj);
        }
    }

    if (flags == 0) {
        flags = RPMSENSE_ANY;
    }

    return flags;
}

/*
 * Return the dependency type key for the given depends dictionary
 * abbreviation, or NULL if it is not a type tarpm knows about.
 */
const char *
dependency_type_key(const char abbrev)
{
    int i = 0;

    for (i = 0; dep_types[i].key != NULL; i++) {
        if (dep_types[i].abbrev == abbrev) {
            return dep_types[i].key;
        }
    }

    return NULL;
}

/*
 * Return the depends dictionary abbreviation for the given dependency
 * type key, or a NUL byte if it is not a type tarpm knows about.
 */
char
dependency_type_abbrev(const char *key)
{
    int i = 0;

    if (key == NULL) {
        return '\0';
    }

    for (i = 0; dep_types[i].key != NULL; i++) {
        if (!strcmp(dep_types[i].key, key)) {
            return dep_types[i].abbrev;
        }
    }

    return '\0';
}

/*
 * Find the entry in dependencies[key] that matches the given
 * dependency entry and return its index.  The name, version, and flag
 * word are everything the header records about a dependency, so
 * entries that agree on all three are the same dependency.
 * Returns -1 if there is no match.
 */
int
dependency_index(struct json_object *dependencies, const char *key, struct json_object *entry)
{
    size_t i = 0;
    size_t count = 0;
    uint32_t flags = 0;
    const char *name = NULL;
    const char *version = NULL;
    struct json_object *deps = NULL;
    struct json_object *candidate = NULL;

    if (dependencies == NULL || key == NULL || entry == NULL) {
        return -1;
    }

    if (!json_object_object_get_ex(dependencies, key, &deps)) {
        return -1;
    }

    if (json_object_get_type(deps) != json_type_array) {
        return -1;
    }

    name = dependency_string(entry, RPM_DEPENDENCY_NAME_DESC);
    version = dependency_string(entry, RPM_DEPENDENCY_VERSION_DESC);
    flags = dependency_entry_flags(entry);
    count = json_object_array_length(deps);

    for (i = 0; i < count; i++) {
        candidate = json_object_array_get_idx(deps, i);

        if (candidate == NULL) {
            continue;
        }

        if (strcmp(name, dependency_string(candidate, RPM_DEPENDENCY_NAME_DESC))) {
            continue;
        }

        if (strcmp(version, dependency_string(candidate, RPM_DEPENDENCY_VERSION_DESC))) {
            continue;
        }

        if (flags != dependency_entry_flags(candidate)) {
            continue;
        }

        return (int) i;
    }

    return -1;
}

/*
 * Read the name, flags, and version arrays for one dependency type
 * out of the header data.  Arrays we do not find stay NULL with a
 * count of zero.
 */
static void
collect_dep_arrays(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct dep_type *dep, struct dep_arrays *da)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    uint32_t flag = 0;
    rpmTagVal tag = 0;
    rpmTagType datatype = 0;
    uint8_t *data = NULL;
    uint8_t *p = NULL;
    struct rpmhdrentry *hdrentry = NULL;

    if (hdr == NULL || hdrinfo == NULL || dep == NULL || da == NULL) {
        return;
    }

    hdrentry = hdrinfo->estart;

    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);
        data = hdrinfo->datastart + offset;

        if (tag == dep->name && datatype == RPM_STRING_ARRAY_TYPE) {
            da->nnames = count;
            da->names = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                da->names[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == dep->flags && datatype == RPM_INT32_TYPE) {
            da->nflags = count;
            da->flags = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&flag, p, sizeof(uint32_t));
                da->flags[j] = ntohl(flag);
                p += sizeof(uint32_t);
            }
        } else if (tag == dep->version && datatype == RPM_STRING_ARRAY_TYPE) {
            da->nversions = count;
            da->versions = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                da->versions[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        }
    }

    return;
}

/*
 * Free what collect_dep_arrays() allocated.
 */
static void
free_dep_arrays(struct dep_arrays *da)
{
    uint32_t i = 0;

    if (da == NULL) {
        return;
    }

    if (da->names != NULL) {
        for (i = 0; i < da->nnames; i++) {
            free(da->names[i]);
        }

        free(da->names);
    }

    if (da->flags != NULL) {
        free(da->flags);
    }

    if (da->versions != NULL) {
        for (i = 0; i < da->nversions; i++) {
            free(da->versions[i]);
        }

        free(da->versions);
    }

    return;
}

/*
 * Generate a dependency array from the three tag arrays (name, flags, version).
 * Returns a JSON array of dependency entries, or NULL if not found.
 */
static struct json_object *
generate_formatted_dependencies(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct dep_type *dep)
{
    uint32_t j = 0;
    char *comparison = NULL;
    struct dep_arrays da;
    struct json_object *deps = NULL;
    struct json_object *entry = NULL;
    struct json_object *sense_flags = NULL;

    if (hdr == NULL || hdrinfo == NULL || dep == NULL) {
        return NULL;
    }

    /* initialize */
    memset(&da, '\0', sizeof(da));

    /* read the three dependency arrays out of the header */
    collect_dep_arrays(hdr, hdrinfo, dep, &da);

    /* No dependency found */
    if (da.names == NULL) {
        free_dep_arrays(&da);
        return NULL;
    }

    /* Build the dependency array */
    deps = json_object_new_array();

    for (j = 0; j < da.nnames; j++) {
        entry = json_object_new_object();

        /* Add name */
        json_object_object_add(entry, RPM_DEPENDENCY_NAME_DESC, json_object_new_string(da.names[j]));

        /* Build comparison and sense flags */
        if (da.flags != NULL && j < da.nflags) {
            comparison = comparison_to_str(da.flags[j]);
            sense_flags = generate_sense_flags(da.flags[j]);
        }

        /* Add the comparison, version, and sense_flags */
        if (comparison != NULL) {
            json_object_object_add(entry, RPM_DEPENDENCY_COMPARISON_DESC, json_object_new_string(comparison));
            free(comparison);
            comparison = NULL;
        }

        if (da.versions != NULL && j < da.nversions && da.versions[j] != NULL && da.versions[j][0] != '\0') {
            json_object_object_add(entry, RPM_DEPENDENCY_VERSION_DESC, json_object_new_string(da.versions[j]));
        }

        if (sense_flags != NULL) {
            json_object_object_add(entry, RPM_SENSE_FLAGS_DESC, sense_flags);
            sense_flags = NULL;
        }

        /* Add this dependency entry to the array */
        json_object_array_add(deps, entry);
    }

    /* Cleanup */
    free_dep_arrays(&da);

    return deps;
}

/*
 * Build all dependency arrays from raw header data.
 * Returns a JSON object with dependency arrays, or NULL if no dependencies found.
 */
struct json_object *
generate_dependencies(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo)
{
    struct json_object *all = NULL;
    struct json_object *deps = NULL;
    const struct dep_type *dep = NULL;
    int i = 0;
    bool has_deps = false;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    all = json_object_new_object();

    for (i = 0; dep_types[i].key != NULL; i++) {
        dep = &dep_types[i];
        deps = generate_formatted_dependencies(hdr, hdrinfo, dep);

        if (deps != NULL && json_object_array_length(deps) > 0) {
            json_object_object_add(all, dep->key, deps);
            has_deps = true;
        } else if (deps != NULL) {
            json_object_put(deps);
        }
    }

    if (!has_deps) {
        json_object_put(all);
        return NULL;
    }

    return all;
}

/*
 * Reconstruct the three dependency tag entries from a dependency array.
 * Adds the three tag entries to the provided tags array.
 */
static void
add_dependency_type_tags(struct json_object *tags, struct json_object *deps, const struct dep_type *dep)
{
    size_t i = 0;
    size_t count = 0;
    struct json_object *entry = NULL;
    struct json_object *obj = NULL;
    struct json_object *names = NULL;
    struct json_object *flags = NULL;
    struct json_object *versions = NULL;
    const char *s = NULL;
    uint32_t flag = 0;
    struct json_object *tag = NULL;

    if (tags == NULL || deps == NULL || dep == NULL) {
        return;
    }

    if (json_object_get_type(deps) != json_type_array) {
        return;
    }

    count = json_object_array_length(deps);

    if (count == 0) {
        return;
    }

    /* Create the three arrays */
    names = json_object_new_array();
    flags = json_object_new_array();
    versions = json_object_new_array();

    /* Process each dependency entry */
    for (i = 0; i < count; i++) {
        entry = json_object_array_get_idx(deps, i);

        if (entry == NULL) {
            continue;
        }

        /* Get name */
        if (json_object_object_get_ex(entry, RPM_DEPENDENCY_NAME_DESC, &obj)) {
            s = json_object_get_string(obj);
            json_object_array_add(names, json_object_new_string(s));
        } else {
            json_object_array_add(names, json_object_new_string(""));
        }

        /* Combine the comparison operators and the other RPMSENSE flags */
        flag = dependency_entry_flags(entry);
        json_object_array_add(flags, json_object_new_int(flag));

        /* Get version */
        if (json_object_object_get_ex(entry, RPM_DEPENDENCY_VERSION_DESC, &obj)) {
            s = json_object_get_string(obj);
            json_object_array_add(versions, json_object_new_string(s));
        } else {
            json_object_array_add(versions, json_object_new_string(""));
        }
    }

    /* Create the NAME tag entry */
    tag = json_object_new_object();
    json_object_object_add(tag, "tag", json_object_new_string(rpmTagGetName(dep->name)));
    json_object_object_add(tag, "type", json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, "value", names);
    json_object_array_add(tags, tag);

    /* Create the FLAGS tag entry */
    tag = json_object_new_object();
    json_object_object_add(tag, "tag", json_object_new_string(rpmTagGetName(dep->flags)));
    json_object_object_add(tag, "type", json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(tag, "value", flags);
    json_object_array_add(tags, tag);

    /* Create the VERSION tag entry */
    tag = json_object_new_object();
    json_object_object_add(tag, "tag", json_object_new_string(rpmTagGetName(dep->version)));
    json_object_object_add(tag, "type", json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(tag, "value", versions);
    json_object_array_add(tags, tag);

    return;
}

/*
 * Reconstruct all dependency tag entries from the dependencies object.
 * Adds tag entries to the provided tags array.
 */
void
add_dependency_tags(struct json_object *tags, struct json_object *dependencies)
{
    struct json_object *deps = NULL;
    const struct dep_type *dep = NULL;
    int i = 0;

    if (tags == NULL || dependencies == NULL) {
        return;
    }

    for (i = 0; dep_types[i].key != NULL; i++) {
        dep = &dep_types[i];

        if (json_object_object_get_ex(dependencies, dep->key, &deps)) {
            add_dependency_type_tags(tags, deps, dep);
        }
    }

    return;
}

/*
 * Check if a tag is a dependency tag that should be excluded from normal tag processing.
 */
bool
is_dependency_tag(rpmTagVal tag)
{
    int i = 0;
    const struct dep_type *dep = NULL;

    for (i = 0; dep_types[i].key != NULL; i++) {
        dep = &dep_types[i];

        if (tag == dep->name || tag == dep->flags || tag == dep->version) {
            return true;
        }
    }

    return false;
}
