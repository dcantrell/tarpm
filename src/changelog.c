/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <errno.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>
#include <time.h>

#include "tarpm.h"

/*
 * Convert Unix timestamp to RPM changelog date format.
 * Returns allocated string like "Wed Jul 29 2020". Caller must free.
 */
static char *
timestamp_to_changelog_date(uint32_t timestamp)
{
    time_t t = (time_t) timestamp;
    struct tm *tm_info = NULL;
    char *result = NULL;

    tm_info = gmtime(&t);

    if (tm_info == NULL) {
        return NULL;
    }

    result = xalloc(64);
    strftime(result, 64, "%a %b %d %Y", tm_info);

    return result;
}

/*
 * Parse RPM changelog date format back to Unix timestamp.
 * Input formats:
 *   - Original (4 words): "Wed Jul 29 2020"
 *   - Extended (6 words): "Thu Oct 6 06:48:39 CEST 2016"
 * Returns Unix timestamp or 0 on error.
 * Note: Original format sets time to noon (12:00:00 UTC) to match RPM convention.
 * Extended format uses the specified time and timezone.
 */
static uint32_t
changelog_date_to_timestamp(const char *changelog_date)
{
    struct tm tm_info = {0};
    time_t t = 0;
    char *result = NULL;
    char *tz_start = NULL;
    char *year_start = NULL;
    char tz_name[32] = {0};
    char *saved_tz = NULL;
    int year = 0;
    size_t tz_len = 0;
    char *endptr = NULL;
    long year_long = 0;

    if (changelog_date == NULL) {
        return 0;
    }

    /* Try extended format first: "Day Mon DD HH:MM:SS TZ YYYY" */
    result = strptime(changelog_date, "%a %b %d %H:%M:%S", &tm_info);

    if (result != NULL) {
        /* Extended format - need to handle timezone and year */
        tz_start = result;

        /* Skip whitespace to timezone */
        while (*tz_start == ' ' || *tz_start == '\t') {
            tz_start++;
        }

        /* Find end of timezone (start of year) */
        year_start = tz_start;

        while (*year_start && *year_start != ' ' && *year_start != '\t') {
            year_start++;
        }

        /* Extract timezone name */
        if (year_start > tz_start) {
            tz_len = year_start - tz_start;

            if (tz_len < sizeof(tz_name)) {
                memcpy(tz_name, tz_start, tz_len);
                tz_name[tz_len] = '\0';
            }
        }

        /* Skip whitespace to year */
        while (*year_start == ' ' || *year_start == '\t') {
            year_start++;
        }

        /* Parse year */
        errno = 0;
        year_long = strtol(year_start, &endptr, 10);

        if (errno != 0 || endptr == year_start || year_long < 1990 || year_long >= 3000) {
            return 0;
        }

        year = (int) year_long;

        tm_info.tm_year = year - 1900;

        /* Set timezone and convert */
        saved_tz = getenv("TZ");

        if (saved_tz != NULL) {
            saved_tz = strdup(saved_tz);
        }

        if (tz_name[0] != '\0') {
            setenv("TZ", tz_name, 1);
        }

        tzset();
        t = mktime(&tm_info);

        /* Restore original timezone */
        if (saved_tz != NULL) {
            setenv("TZ", saved_tz, 1);
            free(saved_tz);
        } else {
            unsetenv("TZ");
        }

        tzset();
    } else {
        /* Try original format: "Day Mon DD YYYY" */
        memset(&tm_info, 0, sizeof(tm_info));

        if (strptime(changelog_date, "%a %b %d %Y", &tm_info) == NULL) {
            return 0;
        }

        /* Set time to noon (12:00:00) to match RPM changelog convention */
        tm_info.tm_hour = 12;
        tm_info.tm_min = 0;
        tm_info.tm_sec = 0;

        /* Original format is always UTC */
        t = timegm(&tm_info);
    }

    if (t == -1) {
        return 0;
    }

    return (uint32_t) t;
}

/*
 * Split text on newlines into a JSON array.
 * Returns allocated json_object array. Caller must free.
 */
static struct json_object *
split_changelog_text(const char *text)
{
    struct json_object *lines = NULL;
    char *copy = NULL;
    char *line = NULL;
    char *sp = NULL;

    if (text == NULL) {
        return json_object_new_array();
    }

    lines = json_object_new_array();
    copy = strdup(text);

    if (copy == NULL) {
        return lines;
    }

    line = strtok_r(copy, "\n\r", &sp);

    while (line != NULL) {
        json_object_array_add(lines, json_object_new_string(line));
        line = strtok_r(NULL, "\n\r", &sp);
    }

    free(copy);
    return lines;
}

/*
 * Join JSON array of strings into text with newlines.
 * Returns allocated string. Caller must free.
 */
static char *
join_changelog_text(struct json_object *lines)
{
    size_t i = 0;
    size_t len = 0;
    size_t total_len = 0;
    char *result = NULL;
    char *pos = NULL;
    struct json_object *line = NULL;
    const char *ls = NULL;

    if (lines == NULL || json_object_get_type(lines) != json_type_array) {
        return strdup("");
    }

    len = json_object_array_length(lines);

    if (len == 0) {
        return strdup("");
    }

    /* Calculate total length needed */
    for (i = 0; i < len; i++) {
        line = json_object_array_get_idx(lines, i);
        total_len += strlen(json_object_get_string(line)) + 1;
    }

    result = xalloc(total_len + 1);
    pos = result;

    for (i = 0; i < len; i++) {
        line = json_object_array_get_idx(lines, i);
        ls = json_object_get_string(line);

        strcpy(pos, ls);
        pos += strlen(ls);

        if (i < len - 1) {
            *pos = '\n';
            pos++;
        }
    }

    *pos = '\0';
    return result;
}

/* Cleanup function called by generate_changelog. */
static void
free_changelog_parts(uint32_t *times, char** names, uint32_t nnames, char **texts, uint32_t ntexts)
{
    uint32_t i = 0;

    free(times);

    if (names != NULL) {
        for (i = 0; i < nnames; i++) {
            free(names[i]);
        }

        free(names);
    }

    if (texts != NULL) {
        for (i = 0; i < ntexts; i++) {
            free(texts[i]);
        }

        free(texts);
    }

    return;
}

/*
 * Build the changelog array from raw header data.
 * Returns a JSON array of changelog entries, or NULL if no changelog found.
 */
struct json_object *
generate_changelog(const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t tag = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    rpmTagType datatype = 0;
    struct rpmhdrentry *hdrentry = NULL;
    uint8_t *data = NULL;
    struct json_object *changelog = NULL;
    struct json_object *entry = NULL;
    uint32_t *times = NULL;
    uint32_t ntimes = 0;
    char **names = NULL;
    uint32_t nnames = 0;
    char **texts = NULL;
    uint32_t ntexts = 0;
    uint8_t *p = NULL;
    uint32_t time_val = 0;
    char *changelog_date = NULL;
    struct json_object *lines = NULL;

    if (hdr == NULL || hdrinfo == NULL) {
        return NULL;
    }

    hdrentry = hdrinfo->estart;

    /* First pass: collect the three changelog arrays */
    for (i = 0; i < hdr->nentries; i++) {
        tag = ntohl(hdrentry[i].tag);
        offset = ntohl(hdrentry[i].offset);
        datatype = ntohl(hdrentry[i].type);
        count = ntohl(hdrentry[i].count);
        data = hdrinfo->datastart + offset;

        if (tag == RPMTAG_CHANGELOGTIME && datatype == RPM_INT32_TYPE) {
            ntimes = count;
            times = xalloc(count * sizeof(uint32_t));
            p = data;

            for (j = 0; j < count; j++) {
                memcpy(&time_val, p, sizeof(uint32_t));
                times[j] = ntohl(time_val);
                p += sizeof(uint32_t);
            }
        } else if (tag == RPMTAG_CHANGELOGNAME && datatype == RPM_STRING_ARRAY_TYPE) {
            nnames = count;
            names = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                names[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        } else if (tag == RPMTAG_CHANGELOGTEXT && datatype == RPM_STRING_ARRAY_TYPE) {
            ntexts = count;
            texts = xalloc(count * sizeof(char *));
            p = data;

            for (j = 0; j < count; j++) {
                texts[j] = strdup((char *) p);
                p += strlen((char *) p) + 1;
            }
        }
    }

    /* No changelog found */
    if (times == NULL || names == NULL || texts == NULL) {
        free_changelog_parts(times, names, nnames, texts, ntexts);
        return NULL;
    }

    /* Verify all arrays have the same length */
    if (ntimes != nnames || ntimes != ntexts) {
        warnx(_("*** changelog arrays have mismatched lengths"));
        free_changelog_parts(times, names, nnames, texts, ntexts);
        return NULL;
    }

    /* Build the changelog array */
    changelog = json_object_new_array();

    for (j = 0; j < ntimes; j++) {
        entry = json_object_new_object();

        /* Add timestamp */
        changelog_date = timestamp_to_changelog_date(times[j]);

        if (changelog_date != NULL) {
            json_object_object_add(entry, "timestamp", json_object_new_string(changelog_date));
            free(changelog_date);
        }

        /* Add name */
        json_object_object_add(entry, "name", json_object_new_string(names[j]));

        /* Add text array (split on newlines) */
        lines = split_changelog_text(texts[j]);
        json_object_object_add(entry, "text", lines);

        json_object_array_add(changelog, entry);
    }

    /* Cleanup */
    free_changelog_parts(times, names, nnames, texts, ntexts);

    return changelog;
}

/*
 * Reconstruct the three changelog tag entries from the changelog array.
 * Adds the three tag entries to the provided tags array.
 */
void
add_changelog_tags(struct json_object *tags, struct json_object *changelog)
{
    size_t i = 0;
    size_t count = 0;
    struct json_object *entry = NULL;
    struct json_object *obj = NULL;
    struct json_object *times = NULL;
    struct json_object *names = NULL;
    struct json_object *texts = NULL;
    const char *s = NULL;
    uint32_t timestamp = 0;
    char *changelog_text = NULL;

    if (tags == NULL || changelog == NULL) {
        return;
    }

    if (json_object_get_type(changelog) != json_type_array) {
        return;
    }

    count = json_object_array_length(changelog);

    if (count == 0) {
        return;
    }

    /* Create the three arrays */
    times = json_object_new_array();
    names = json_object_new_array();
    texts = json_object_new_array();

    /* Process each changelog entry */
    for (i = 0; i < count; i++) {
        entry = json_object_array_get_idx(changelog, i);

        if (entry == NULL) {
            continue;
        }

        /* Get timestamp and convert back to Unix time */
        if (json_object_object_get_ex(entry, "timestamp", &obj)) {
            s = json_object_get_string(obj);
            timestamp = changelog_date_to_timestamp(s);
            json_object_array_add(times, json_object_new_int(timestamp));
        }

        /* Get name */
        if (json_object_object_get_ex(entry, "name", &obj)) {
            s = json_object_get_string(obj);
            json_object_array_add(names, json_object_new_string(s));
        }

        /* Get text array and join back to string with newlines */
        if (json_object_object_get_ex(entry, "text", &obj)) {
            changelog_text = join_changelog_text(obj);
            json_object_array_add(texts, json_object_new_string(changelog_text));
            free(changelog_text);
        }
    }

    /* Create the CHANGELOGTIME tag entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string(rpmTagGetName(RPMTAG_CHANGELOGTIME)));
    json_object_object_add(entry, "type", json_object_new_string(strtagtype(RPM_INT32_TYPE)));
    json_object_object_add(entry, "value", times);
    json_object_array_add(tags, entry);

    /* Create the CHANGELOGNAME tag entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string(rpmTagGetName(RPMTAG_CHANGELOGNAME)));
    json_object_object_add(entry, "type", json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(entry, "value", names);
    json_object_array_add(tags, entry);

    /* Create the CHANGELOGTEXT tag entry */
    entry = json_object_new_object();
    json_object_object_add(entry, "tag", json_object_new_string(rpmTagGetName(RPMTAG_CHANGELOGTEXT)));
    json_object_object_add(entry, "type", json_object_new_string(strtagtype(RPM_STRING_ARRAY_TYPE)));
    json_object_object_add(entry, "value", texts);
    json_object_array_add(tags, entry);

    return;
}
