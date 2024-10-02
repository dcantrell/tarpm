/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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
 * Extract the data of the RPM lead and convert it to JSON data.
 * Returns 0 on success, -1 on error.
 */
int
extract_lead(const int fd, const char *output_dir)
{
    struct rpmlead lead;
    struct json_object *out = NULL;
    char *s = NULL;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* zero out the lead structure */
    memset(&lead, 0, sizeof(lead));

    /* read in the lead */
    if (read(fd, &lead, RPMLEAD_SIZE) != RPMLEAD_SIZE) {
        err(EXIT_FAILURE, "read");
    }

    /* convert some lead fields from network byte order to host byte order */
    lead.type = ntohs(lead.type);
    lead.osnum = ntohs(lead.osnum);
    lead.archnum = ntohs(lead.archnum);
    lead.signature_type = ntohs(lead.signature_type);

    /* generate a JSON structure for the lead */
    out = json_object_new_object();

    xasprintf(&s, "0x%hhX%hhX%hhX%hhX", lead.magic[0], lead.magic[1], lead.magic[2], lead.magic[3]);
    json_object_object_add(out, RPM_LEAD_MAGIC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d.%d", lead.major, lead.minor);
    json_object_object_add(out, RPM_LEAD_VERSION, json_object_new_string(s));
    free(s);

    if (lead.type) {
        json_object_object_add(out, RPM_LEAD_TYPE, json_object_new_string(RPM_LEAD_SOURCE));
    } else {
        json_object_object_add(out, RPM_LEAD_TYPE, json_object_new_string(RPM_LEAD_BINARY));
    }

    json_object_object_add(out, RPM_LEAD_NAME, json_object_new_string(lead.name));

    xasprintf(&s, "%hu", lead.archnum);
    json_object_object_add(out, RPM_LEAD_ARCH, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%hu", lead.osnum);
    json_object_object_add(out, RPM_LEAD_OS, json_object_new_string(s));
    free(s);

    if (lead.signature_type == 5) {
        json_object_object_add(out, RPM_LEAD_SIGTYPE, json_object_new_string(RPM_LEAD_HEADERSIG));
    } else {
        json_object_object_add(out, RPM_LEAD_SIGTYPE, json_object_new_string(RPM_LEAD_UNKNOWN));
    }

    /* write the lead to a JSON file */
    if (write_json_file(out, output_dir, OUTPUT_LEAD) != 0) {
        warn("write_json_file");
    }

    /* cleanup */
    free_json(out);

    return 0;
}
