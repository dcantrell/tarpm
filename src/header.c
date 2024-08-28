/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
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
extract_lead(const char *rpm, const char *output_dir)
{
    int fd;
    struct rpmlead lead;
    struct json_object *out = NULL;
    struct json_object_iter iter;
    int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;
    char *s = NULL;
    int r = 0;
    FILE *fp = NULL;
    const char *js = NULL;

    assert(rpm != NULL);
    assert(output_dir != NULL);

    /* zero out the lead structure */
    memset(&lead, 0, sizeof(lead));

    /* open the package for reading */
    fd = open(rpm, O_RDONLY);

    if (fd == -1) {
        err(EXIT_FAILURE, "open");
    }

    /* read in the lead */
    if (read(fd, &lead, sizeof(lead)) != sizeof(lead)) {
        err(EXIT_FAILURE, "read");
    }

    /* close the package */
    if (close(fd) == -1) {
        warn("close");
    }

    /* convert some lead fields from network byte order to host byte order */
    lead.type = ntohs(lead.type);
    lead.osnum = ntohs(lead.osnum);
    lead.archnum = ntohs(lead.archnum);
    lead.signature_type = ntohs(lead.signature_type);

    /* generate a JSON structure for the lead */
    out = json_object_new_object();

    xasprintf(&s, "0x%hhX%hhX%hhX%hhX", lead.magic[0], lead.magic[1], lead.magic[2], lead.magic[3]);
    json_object_object_add(out, "Lead Magic", json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d.%d", lead.major, lead.minor);
    json_object_object_add(out, "Version", json_object_new_string(s));
    free(s);

    if (lead.type) {
        json_object_object_add(out, "Type", json_object_new_string("RPMLEAD_SOURCE"));
    } else {
        json_object_object_add(out, "Type", json_object_new_string("RPMLEAD_BINARY"));
    }

    json_object_object_add(out, "Name", json_object_new_string(lead.name));

    xasprintf(&s, "%hu", lead.archnum);
    json_object_object_add(out, "Arch", json_object_new_string(s));
    free(s);

    xasprintf(&s, "%hu", lead.osnum);
    json_object_object_add(out, "OS", json_object_new_string(s));
    free(s);

    if (lead.signature_type == 5) {
        json_object_object_add(out, "Signature Type", json_object_new_string("RPMSIG_HEADERSIG"));
    } else {
        json_object_object_add(out, "Signature Type", json_object_new_string("unknown"));
    }

    /* write the lead to a JSON file */
    s = joinpath(output_dir, "lead.json", NULL);
    assert(s != NULL);

    fp = fopen(s, "w");

    if (fp == NULL) {
        warn("fopen");
        return -1;
    }

    js = json_object_to_json_string_ext(out, flags);

    if (js == NULL) {
        errx(EXIT_FAILURE, "unable to turn JSON object in to string");
    }

    fprintf(fp, "%s\n", js);
    r = fflush(fp);

    if (r != 0) {
        warn("fflush");
    }

    r = fclose(fp);

    if (r != 0) {
        warn("fclose");
    }

    /* clean up the JSON object memory usage */
    json_object_object_foreachC(out, iter) {
        json_object_put(iter.val);
    }

    return 0;
}

/*
 * Iterate over the RPM header and write the data to a JSON file in
 * output_dir.  Returns 0 on success, -1 on error.
 */
int
extract_header(Header h, const char *output_dir)
{
    rpmtd td = NULL;
    HeaderIterator hi = NULL;
    rpmTagType t = 0;

    assert(h != NULL);
    assert(output_dir != NULL);

    /* create a new tag data container */
    td = rpmtdNew();

    /* create a header iteator to walk the tags */
    hi = headerInitIterator(h);

    /* iterate over the header tags and add them to a JSON structure */
    while (headerNext(hi, td) == 1) {
        t = rpmtdType(td);

printf("tag=|%s (%d)|\n", rpmTagGetName(t), t);

        rpmtdReset(td);
    }

    /* clean up */
    headerFreeIterator(hi);
    rpmtdFreeData(td);
    rpmtdFree(td);

    return 0;
}
