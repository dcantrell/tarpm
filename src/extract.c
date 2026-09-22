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

/*
 * Write one of the JSON metadata files.  With no path given the file
 * lands in dest_dir under its usual name.  Returns 0 on success, -1
 * on error.
 */
static int
write_metadata(struct json_object *data, const char *dest_dir, const char *path, const char *name)
{
    int r = 0;
    char *dir = NULL;
    char *base = NULL;

    if (path == NULL) {
        return write_json_file(data, dest_dir, name);
    }

    dir = dir_name(path);
    base = base_name(path);
    r = write_json_file(data, dir, base);

    free(dir);
    free(base);

    return r;
}

/* Handler for -x mode (extract) */
int
extract_rpm(const char *filename, const char *cwd, const char *output_dir, const struct json_paths *paths, const bool verbose)
{
    int r = 0;
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int rpmfd = -1;
    char *candidate_path = NULL;
    char *tmp = NULL;
    char *payload_file = NULL;
    char *dest_dir = NULL;
    char *header_dir = NULL;
    const char *lead_path = NULL;
    const char *signature_path = NULL;
    const char *header_path = NULL;
    const char *payload_path = NULL;
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

    /* where the caller asked us to put the JSON metadata files */
    if (paths != NULL) {
        lead_path = paths->lead;
        signature_path = paths->signature;
        header_path = paths->header;
        payload_path = paths->payload;
    }

    /* tag values written to their own file sit next to header.json */
    if (header_path == NULL) {
        header_dir = strdup(dest_dir);
    } else {
        header_dir = dir_name(header_path);
    }

    if (header_dir == NULL) {
        warnx(_("*** unable to set header_dir"));
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
    header = read_header(rpmfd, header_dir);

    if (header == NULL) {
        warnx("read_header");
        return -1;
    }

    /* close the RPM after reading headers */
    if (close(rpmfd) == -1) {
        warn("close");
    }

    /* write out the header metadata */
    if (write_metadata(lead, dest_dir, lead_path, OUTPUT_LEAD) != 0) {
        warn("write_json_file");
    }

    if (write_metadata(signature, dest_dir, signature_path, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    if (write_metadata(header, dest_dir, header_path, OUTPUT_HEADER) != 0) {
        warn("write_json_file");
    }

    /* unpack the RPM payload where the caller asked us to */
    if (payload_path == NULL) {
        xasprintf(&tmp, "%s/%s", dest_dir, PAYLOAD_SUBDIR);
    } else {
        tmp = strdup(payload_path);
    }

    if (tmp == NULL) {
        warnx(_("*** unable to set the payload directory"));
        return -1;
    }

    if (mkdirp(tmp, mode) == -1) {
        warnx("mkdirp");
        free(tmp);
        return -1;
    }

    /*
     * Try to extract straight from the RPM with libarchive.  We
     * still go through the Fdopen() call in librpm, which can fail
     * on some compression types depending on the librpm version.
     */
    r = unpack_archive(filename, tmp, verbose);

    if (r != 0) {
        /*
         * Direct extraction failed, so fall back to
         * convert_payload().  The failure would be unpack_archive()
         * not getting libarchive to take the file handle Fdopen() in
         * librpm gave us.
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

        if (unpack_archive(payload_file, tmp, verbose) != 0) {
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
    free(header_dir);
    headerFree(h);

    return 0;
}
