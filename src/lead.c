/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
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
        return NULL;
    }

    /* NUL terminate the name */
    rawlead.name[sizeof(rawlead.name) - 1] = '\0';

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

    /*
     * the RPM lead is read-only to us because we always make it
     * from scratch
     */
    json_object_object_add(lead, RPM_METADATA_READ_ONLY, json_object_new_string("true"));

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
    const char *n = NULL;
    const char *e = NULL;
    const char *v = NULL;
    const char *r = NULL;
    char *nevr = NULL;

    if (header == NULL) {
        return NULL;
    }

    /* allocate lead structure */
    lead = xalloc(sizeof(*lead));

    /* get the tags array */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &obj) == 0) {
        warnx(_("*** missing tags in header.json"));
        free(lead);
        return NULL;
    }

    /* fill out the lead */

    /*
     * The RPM lead is deprecated, but the major version still tells a
     * v4 package apart from a v6 one.
     * rpmLeadFromHeader() in lib/rpmlead.cc in the rpm source goes by
     * whether the header carries RPMTAG_RPMFORMAT, so we do the same.
     */
    if (get_tag_value(obj, rpmTagGetName(RPMTAG_RPMFORMAT_VALUE)) != NULL) {
        lead->major = RPM_LEAD_MAJOR_V6;
    } else {
        lead->major = RPM_LEAD_MAJOR_V4;
    }

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
    if (get_tag_value(obj, rpmTagGetName(RPMTAG_SOURCEPACKAGE)) != NULL) {
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

    /* only write the epoch value to the nevr string if it is >0 */
    if (e && strcmp(e, "0")) {
        xasprintf(&nevr, "%s-%s:%s-%s", n, e, v, r);
    } else {
        xasprintf(&nevr, "%s-%s-%s", n, v, r);
    }

#ifdef _HAVE_STRLCPY
    strlcpy(lead->name, nevr, sizeof(lead->name));
#else
    snprintf(lead->name, sizeof(lead->name), "%s", nevr);
#endif
    free(nevr);

    return lead;
}
