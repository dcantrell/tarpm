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

static const struct dep_type dep_types[] = {
    { "provides",    RPMTAG_PROVIDENAME,    RPMTAG_PROVIDEFLAGS,    RPMTAG_PROVIDEVERSION    },
    { "requires",    RPMTAG_REQUIRENAME,    RPMTAG_REQUIREFLAGS,    RPMTAG_REQUIREVERSION    },
    { "conflicts",   RPMTAG_CONFLICTNAME,   RPMTAG_CONFLICTFLAGS,   RPMTAG_CONFLICTVERSION   },
    { "obsoletes",   RPMTAG_OBSOLETENAME,   RPMTAG_OBSOLETEFLAGS,   RPMTAG_OBSOLETEVERSION   },
    { "recommends",  RPMTAG_RECOMMENDNAME,  RPMTAG_RECOMMENDFLAGS,  RPMTAG_RECOMMENDVERSION  },
    { "suggests",    RPMTAG_SUGGESTNAME,    RPMTAG_SUGGESTFLAGS,    RPMTAG_SUGGESTVERSION    },
    { "supplements", RPMTAG_SUPPLEMENTNAME, RPMTAG_SUPPLEMENTFLAGS, RPMTAG_SUPPLEMENTVERSION },
    { "enhances",    RPMTAG_ENHANCENAME,    RPMTAG_ENHANCEFLAGS,    RPMTAG_ENHANCEVERSION    },
    { NULL, 0, 0, 0 }
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

    if (flags & RPMSENSE_META) {
        json_object_array_add(sense_flags, json_object_new_string(SENSE_FLAG_META));
    }

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
        } else if (!strcmp(s, SENSE_FLAG_META)) {
            flags |= RPMSENSE_META;
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

/* Cleanup function called by generate_formatted_dependencies() */
static void
cleanup_deps(char **names, uint32_t nnames, uint32_t *flags_array, char **versions, uint32_t nversions)
{
    uint32_t i = 0;

    if (names != NULL) {
        for (i = 0; i < nnames; i++) {
            free(names[i]);
        }

        free(names);
    }

    if (flags_array != NULL) {
        free(flags_array);
    }

    if (versions != NULL) {
        for (i = 0; i < nversions; i++) {
            free(versions[i]);
        }

        free(versions);
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
    uint32_t i = 0;
    uint32_t j = 0;
    rpmTagVal tag = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    rpmTagType datatype = 0;
    struct rpmhdrentry *hdrentry = NULL;
    uint8_t *data = NULL;
    struct json_object *deps = NULL;
    struct json_object *entry = NULL;
    char **names = NULL;
    uint32_t nnames = 0;
    uint32_t *flags = NULL;
    uint32_t nflags = 0;
    char **versions = NULL;
    uint32_t nversions = 0;
    uint8_t *p = NULL;
    uint32_t flag = 0;
    struct json_object *sense_flags = NULL;
    char *comparison = NULL;

    if (hdr == NULL || hdrinfo == NULL || dep == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;

    /* First pass: collect the three dependency arrays */
    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);
        data = hdrinfo->datastart + offset;

        if (tag == dep->name && datatype == RPM_STRING_ARRAY_TYPE) {
            nnames = count;
            names = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                names[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == dep->flags && datatype == RPM_INT32_TYPE) {
            nflags = count;
            flags = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&flag, p, sizeof(uint32_t));
                flags[j] = ntohl(flag);
                p += sizeof(uint32_t);
            }
        } else if (tag == dep->version && datatype == RPM_STRING_ARRAY_TYPE) {
            nversions = count;
            versions = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                versions[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        }
    }

    /* No dependency found */
    if (names == NULL) {
        cleanup_deps(names, nnames, flags, versions, nversions);
        return NULL;
    }

    /* Build the dependency array */
    deps = json_object_new_array();

    for (j = 0; j < nnames; j++) {
        entry = json_object_new_object();

        /* Add name */
        json_object_object_add(entry, RPM_DEPENDENCY_NAME_DESC, json_object_new_string(names[j]));

        /* Build comparison and sense flags */
        if (flags != NULL && j < nflags) {
            comparison = comparison_to_str(flags[j]);
            sense_flags = generate_sense_flags(flags[j]);
        }

        /* Add the comparison, version, and sense_flags */
        if (comparison != NULL) {
            json_object_object_add(entry, RPM_DEPENDENCY_COMPARISON_DESC, json_object_new_string(comparison));
            free(comparison);
            comparison = NULL;
        }

        if (versions != NULL && j < nversions && versions[j] != NULL && versions[j][0] != '\0') {
            json_object_object_add(entry, RPM_DEPENDENCY_VERSION_DESC, json_object_new_string(versions[j]));
        }

        if (sense_flags != NULL) {
            json_object_object_add(entry, RPM_SENSE_FLAGS_DESC, sense_flags);
            sense_flags = NULL;
        }

        /* Add this dependency entry to the array */
        json_object_array_add(deps, entry);
    }

    /* Cleanup */
    cleanup_deps(names, nnames, flags, versions, nversions);

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
    uint32_t comparison_flags = 0;
    uint32_t other_flags = 0;
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
        comparison_flags = 0;
        other_flags = 0;

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

        /* Get comparison (comparison operators) */
        if (json_object_object_get_ex(entry, RPM_DEPENDENCY_COMPARISON_DESC, &obj)) {
            s = json_object_get_string(obj);
            comparison_flags = str_to_comparison(s);
        }

        /* Get sense_flags (other RPMSENSE flags) */
        if (json_object_object_get_ex(entry, RPM_SENSE_FLAGS_DESC, &obj)) {
            other_flags = read_sense_flags(obj);
        }

        /* Combine comparison and other flags */
        flag = comparison_flags | other_flags;

        if (flag == 0) {
            flag = RPMSENSE_ANY;
        }

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
