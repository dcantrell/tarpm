/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>

#include "tarpm.h"

static unsigned char const lead_magic[] = {
    RPMLEAD_MAGIC0,
    RPMLEAD_MAGIC1,
    RPMLEAD_MAGIC2,
    RPMLEAD_MAGIC3
};

/*
 * Read the data of the RPM lead and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_lead(const int fd)
{
    struct rpmlead rawlead;
    struct json_object *lead = NULL;
    char *s = NULL;

    if (fd < 0) {
        return NULL;
    }

    /* zero out the lead structure */
    memset(&rawlead, 0, sizeof(rawlead));

    /* read in the lead */
    if (!xread(fd, &rawlead, RPMLEAD_SIZE)) {
        exit(EXIT_FAILURE);
    }

    /* convert some lead fields from network byte order to host byte order */
    rawlead.type = ntohs(rawlead.type);
    rawlead.osnum = ntohs(rawlead.osnum);
    rawlead.archnum = ntohs(rawlead.archnum);
    rawlead.signature_type = ntohs(rawlead.signature_type);

    /* generate a JSON structure for the lead */
    lead = json_object_new_object();

    xasprintf(&s, "0x%hhX%hhX%hhX%hhX", rawlead.magic[0], rawlead.magic[1], rawlead.magic[2], rawlead.magic[3]);
    json_object_object_add(lead, RPM_LEAD_MAGIC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d.%d", rawlead.major, rawlead.minor);
    json_object_object_add(lead, RPM_LEAD_VERSION, json_object_new_string(s));
    free(s);

    if (rawlead.type) {
        json_object_object_add(lead, RPM_LEAD_TYPE, json_object_new_string("source"));
    } else {
        json_object_object_add(lead, RPM_LEAD_TYPE, json_object_new_string("binary"));
    }

    json_object_object_add(lead, RPM_LEAD_NAME, json_object_new_string(rawlead.name));
    json_object_object_add(lead, RPM_LEAD_ARCH, json_object_new_int(rawlead.archnum));
    json_object_object_add(lead, RPM_LEAD_OS, json_object_new_int(rawlead.osnum));
    json_object_object_add(lead, RPM_LEAD_SIGTYPE, json_object_new_int(rawlead.signature_type));

    return lead;
}

/*
 * Given RPM header metadata, create a new RPM lead data structure.
 * Returns NULL on failure.  Caller must free the returned structure.
 */
struct rpmlead *
create_lead(struct json_object *header)
{
    struct rpmlead *lead = NULL;
    struct json_object *obj = NULL;
    struct json_object *entry = NULL;
    struct json_object *key = NULL;
    const char *n = NULL;
    const char *e = NULL;
    const char *v = NULL;
    const char *r = NULL;
    const char *name = NULL;
    char *nevr = NULL;
    size_t i = 0;
    bool is_source = false;

    assert(header != NULL);

    /* allocate lead structure */
    lead = xalloc(sizeof(*lead));
    assert(lead != NULL);

    /* get the tags array */
    if (json_object_object_get_ex(header, "tags", &obj) == 0) {
        warnx(_("*** missing tags in header.json"));
        free(lead);
        return NULL;
    }

    /* fill out the lead */

    /* RPM lead is deprecated, all modern RPMs use version 3.0 */
    lead->major = 3;
    lead->minor = 0;
    lead->signature_type = htons(RPMSIGTYPE_HEADERSIG);
    memcpy(lead->magic, lead_magic, sizeof(lead->magic));

    /* the archnum and osnum are always zero, legacy now */
    lead->archnum = htons(0);
    lead->osnum = htons(0);

    /*
     * this is RPMTAG_SOURCEPACKAGE and if it's present, it means we
     * are looking at a source package which is type 1 in the lead,
     * otherwise binary packages are type 0.
     */
    for (i = 0; i < json_object_array_length(obj); i++) {
        entry = json_object_array_get_idx(obj, i);

        if (json_object_object_get_ex(entry, "name", &key) == 1) {
            name = json_object_get_string(key);

            if (strcmp(name, "Sourcepackage") == 0) {
                is_source = true;
                break;
            }
        }
    }

    if (is_source) {
        lead->type = htons(1);
    } else {
        lead->type = htons(0);
    }

    /* construct the NEVR string */

    n = get_tag_value(obj, "Name");
    v = get_tag_value(obj, "Version");
    r = get_tag_value(obj, "Release");

    if (n == NULL || v == NULL || r == NULL) {
        free(lead);
        return NULL;
    }

    e = get_tag_value(obj, "Epoch");

    if (e && strcmp(e, "0")) {
        xasprintf(&nevr, "%s-%s:%s-%s", n, e, v, r);
    } else {
        xasprintf(&nevr, "%s-%s-%s", n, v, r);
    }

    assert(nevr != NULL);
    strlcpy(lead->name, nevr, sizeof(lead->name));
    free(nevr);

    return lead;
}
