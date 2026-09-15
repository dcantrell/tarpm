/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <rpm/header.h>
#include <json_object.h>
#include <archive.h>

#include "tarpm.h"

/* Handler for -x mode (extract) */
int
extract_rpm(const char *filename, const char *cwd, const char *output_dir, const bool verbose)
{
    int r = 0;
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

    if (cwd == NULL) {
        warnx(_("missing cwd in %s call"), __func__);
        return -1;
    }

    if (filename == NULL) {
        warnx(_("missing filename in %s call"), __func__);
        return -1;
    }

    /* validate the specified file is an RPM */
    h = get_header(filename);

    if (h == NULL) {
        warnx(_("*** %s is not a valid RPM"), filename);
        return -1;
    }

    /* make a unique output directory name if we need to */
    if (output_dir == NULL) {
        tmp = get_nevra(h);

        if (tmp == NULL) {
            warnx(_("unable to read NEVRA from RPM header"));
            return -1;
        }

        xasprintf(&candidate_path, "%s/%s", cwd, tmp);
        dest_dir = abspath(candidate_path);

        free(candidate_path);
        free(tmp);
    } else {
        dest_dir = strdup(output_dir);
    }

    if (dest_dir == NULL) {
        warnx(_("*** unable to set dest_dir"));
        return -1;
    }

    /* create the output directory */
    if (mkdirp(dest_dir, mode) == -1) {
        warnx("mkdirp");
        return -1;
    }

    /* open the RPM file (this handle will be passed around) */
    rpmfd = open(filename, O_RDONLY);

    if (rpmfd == -1) {
        warn("open");
        return -1;
    }

    /* extract the RPM lead -- the first header (unused) */
    lead = read_lead(rpmfd);

    if (lead == NULL) {
        warnx("read_lead");
        return -1;
    }

    /* extract the RPM signature -- the second header (sort of used) */
    signature = read_signature(rpmfd);

    if (signature == NULL) {
        warnx("read_signature");
        return -1;
    }

    /* extract the RPM header -- the third header (used) */
    header = read_header(rpmfd, dest_dir);

    if (header == NULL) {
        warnx("read_header");
        return -1;
    }

    /* close the RPM after reading headers */
    if (close(rpmfd) == -1) {
        warn("close");
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

    /* unpack the RPM payload */
    xasprintf(&tmp, "%s/%s", dest_dir, PAYLOAD_SUBDIR);

    if (mkdirp(tmp, mode) == -1) {
        warnx("mkdirp");
        return -1;
    }

    /*
     * Try direct extraction from the RPM using our extract function
     * that uses libarchive.  However, we do make use of librpm's
     * Fdopen() call which can fail on some compression types
     * depending on the version of librpm in use.
     */
    r = unpack_archive(filename, tmp, false, verbose);

    if (r != 0) {
        /*
         * Direct extraction failed, so fall back to convert_payload
         * approach.  The failure here would be from unpack_archive()
         * failing to use libarchive on a file handle from Fdopen() from
         * librpm.
         */
        if (chdir(dest_dir) == -1) {
            warn("chdir");
            return -1;
        }

        payload_file = convert_payload(filename);

        if (payload_file == NULL) {
            warnx("convert_payload");
            return -1;
        }

        if (chdir(cwd) == -1) {
            warn("chdir");
            return -1;
        }

        if (unpack_archive(payload_file, tmp, false, verbose) != 0) {
            warnx("unpack_archive");
            return -1;
        }

        if (unlink(payload_file) == -1) {
            warn("unlink");
            return -1;
        }

        free(payload_file);
    }

    /* clean up */
    json_object_put(header);
    json_object_put(signature);
    json_object_put(lead);
    free(tmp);
    free(dest_dir);
    headerFree(h);

    return 0;
}
