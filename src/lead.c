/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>

#include "tarpm.h"

/*
 * Read the data of the RPM lead and convert it to JSON data.
 * Returns an allocated json_object (caller must free), NULL on error.
 */
struct json_object *
read_lead_from_rpm(const int fd)
{
    struct rpmlead rawlead;
    struct json_object *lead = NULL;
    char *s = NULL;

    if (fd <= 0) {
        return NULL;
    }

    /* zero out the lead structure */
    memset(&rawlead, 0, sizeof(rawlead));

    /* read in the lead */
    if (read(fd, &rawlead, RPMLEAD_SIZE) != RPMLEAD_SIZE) {
        err(EXIT_FAILURE, "read");
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
