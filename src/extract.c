/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <rpm/header.h>
#include <json_object.h>
#include <archive.h>

#include "tarpm.h"

/* Handler for -x mode (extract) */
void
extract_rpm(const char *filename, const char *cwd, const char *output_dir, const bool verbose)
{
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int rpmfd = -1;
    char *candidate_path = NULL;
    char *tmp = NULL;
    char *payload_file = NULL;
    char *dest_dir = NULL;
    Header h;
    struct json_object *lead = NULL;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;

    assert(cwd != NULL);
    assert(filename != NULL);

    /* validate the specified file is an RPM */
    h = get_header(filename);

    if (h == NULL) {
        errx(EXIT_FAILURE, _("*** %s is not a valid RPM"), filename);
    }

    /* open the RPM file (this handle will be passed around) */
    rpmfd = open(filename, O_RDONLY);

    if (rpmfd == -1) {
        err(EXIT_FAILURE, "open");
    }

    /* extract the RPM lead -- the first header (unused) */
    lead = read_lead(rpmfd);

    if (lead == NULL) {
        err(EXIT_FAILURE, "read_lead");
    }

    /* extract the RPM signature -- the second header (sort of used) */
    signature = read_signature(rpmfd);

    if (signature == NULL) {
        err(EXIT_FAILURE, "read_signature");
    }

    /* extract the RPM header -- the third header (used) */
    header = read_header(rpmfd);

    if (header == NULL) {
        err(EXIT_FAILURE, "read_header");
    }

    /* close the RPM after reading headers */
    if (close(rpmfd) == -1) {
        warn("close");
    }

    /* make a unique output directory name if we need to */
    if (output_dir == NULL) {
        tmp = get_nevra(h);
        assert(tmp != NULL);

        xasprintf(&candidate_path, "%s/%s", cwd, tmp);
        assert(candidate_path != NULL);

        dest_dir = abspath(candidate_path);

        free(candidate_path);
        free(tmp);
    } else {
        dest_dir = strdup(output_dir);
    }

    assert(dest_dir != NULL);

    /* create the output directory */
    if (mkdirp(dest_dir, mode) == -1) {
        err(EXIT_FAILURE, "mkdirp");
    }

    /* write out the header metadata */
    if (write_json_file(lead, dest_dir, OUTPUT_LEAD) != 0) {
        warn("write_json_file");
    }

    if (write_json_file(signature, dest_dir, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    if (write_json_file(header, dest_dir, OUTPUT_HEADER) != 0) {
        warn("write_json_file");
    }

    /* extract the RPM payload as an archive we can read in libarchive */
    if (chdir(dest_dir) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    payload_file = extract_payload(filename);

    if (payload_file == NULL) {
        errx(EXIT_FAILURE, "extract_payload");
    }

    if (chdir(cwd) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    /* unpack the RPM payload */
    xasprintf(&tmp, "%s/%s", dest_dir, PAYLOAD_SUBDIR);
    assert(tmp != NULL);

    if (mkdirp(tmp, mode) == -1) {
        err(EXIT_FAILURE, "mkdir");
    }

    if (unpack_archive(payload_file, tmp, false, verbose) != 0) {
        err(EXIT_FAILURE, "unpack_archive");
    }

    if (unlink(payload_file) == -1) {
        err(EXIT_FAILURE, "unlink");
    }

    json_object_put(header);
    json_object_put(signature);
    json_object_put(lead);
    free(payload_file);
    free(tmp);
    free(dest_dir);
    headerFree(h);

    return;
}
