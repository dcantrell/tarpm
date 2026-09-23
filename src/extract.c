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
 * lands in dest_dir under its usual name.  A path of a single hyphen
 * sends the JSON to stdout.  Returns 0 on success, -1 on error.
 */
static int
write_metadata(struct json_object *data, const char *dest_dir, const char *path, const char *name)
{
    int r = 0;
    char *file = NULL;

    if (path != NULL) {
        return write_json_file(data, path);
    }

    file = joinpath(dest_dir, name, NULL);

    if (file == NULL) {
        warn("joinpath");
        return -1;
    }

    r = write_json_file(data, file);

    free(file);

    return r;
}

/*
 * Work out where the extracted RPM goes.  We use the directory the
 * caller gave us or we make a name from the NEVRA.  The caller has to
 * free it.
 */
static char *
extract_dir(Header h, const char *cwd, const char *output_dir)
{
    char *nevra = NULL;
    char *path = NULL;
    char *dir = NULL;

    if (output_dir != NULL) {
        return strdup(output_dir);
    }

    nevra = get_nevra(h);

    if (nevra == NULL) {
        warnx(_("unable to read NEVRA from RPM header"));
        return NULL;
    }

    path = joinpath(cwd, nevra, NULL);
    free(nevra);

    if (path == NULL) {
        warn("joinpath");
        return NULL;
    }

    dir = abspath(path);
    free(path);

    return dir;
}

/*
 * Read the lead, the signature, and the header out of the RPM and
 * write each one to its JSON file.  Returns 0 if that worked and -1
 * if it did not.
 */
static int
extract_metadata(const char *filename, const char *dest_dir, const char *header_dir, const struct json_paths *paths)
{
    int rpmfd = -1;
    struct json_object *lead = NULL;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;

    if (filename == NULL || dest_dir == NULL || header_dir == NULL || paths == NULL) {
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
    if (write_metadata(lead, dest_dir, paths->lead, OUTPUT_LEAD) != 0) {
        warn("write_json_file");
    }

    if (write_metadata(signature, dest_dir, paths->signature, OUTPUT_SIGNATURE) != 0) {
        warn("write_json_file");
    }

    if (write_metadata(header, dest_dir, paths->header, OUTPUT_HEADER) != 0) {
        warn("write_json_file");
    }

    json_object_put(header);
    json_object_put(signature);
    json_object_put(lead);

    return 0;
}

/*
 * Make the directory we unpack the payload in to.  We use the one the
 * caller gave us or we put it under dest_dir.  The caller has to free
 * it.
 */
static char *
make_payload_dir(const char *dest_dir, const char *payload_path, const mode_t mode)
{
    char *dir = NULL;

    if (payload_path == NULL) {
        dir = joinpath(dest_dir, PAYLOAD_SUBDIR, NULL);
    } else {
        dir = strdup(payload_path);
    }

    if (dir == NULL) {
        warnx(_("*** unable to set the payload directory"));
        return NULL;
    }

    if (mkdirp(dir, mode) == -1) {
        warnx("mkdirp");
        free(dir);
        return NULL;
    }

    return dir;
}

/*
 * Unpack the RPM payload in to payload_dir.  We hand the RPM to
 * libarchive first and pull the payload out to a file of its own if
 * that does not work.  Returns 0 if it worked and -1 if it did not.
 */
static int
extract_payload(const char *filename, const char *cwd, const char *dest_dir, const char *payload_dir, const bool verbose)
{
    char *payload_file = NULL;

    /*
     * Try to extract straight from the RPM with libarchive.  We
     * still go through the Fdopen() call in librpm, which can fail
     * on some compression types depending on the librpm version.
     */
    if (unpack_archive(filename, payload_dir, verbose) == 0) {
        return 0;
    }

    /*
     * Direct extraction failed, so fall back to convert_payload().
     * The failure would be unpack_archive() not getting libarchive to
     * take the file handle Fdopen() in librpm gave us.
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

    if (unpack_archive(payload_file, payload_dir, verbose) != 0) {
        warnx("unpack_archive");
        return -1;
    }

    if (unlink(payload_file) == -1) {
        warn("unlink");
        return -1;
    }

    free(payload_file);

    return 0;
}

/* Handler for -x mode (extract) */
int
extract_rpm(const char *filename, const char *cwd, const char *output_dir, const struct json_paths *paths, const bool verbose)
{
    int mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    char *dest_dir = NULL;
    char *header_dir = NULL;
    char *payload_dir = NULL;
    struct json_paths nopaths;
    Header h;

    if (cwd == NULL) {
        warnx(_("missing cwd in %s call"), __func__);
        return -1;
    }

    if (filename == NULL) {
        warnx(_("missing filename in %s call"), __func__);
        return -1;
    }

    /* a caller with nothing to say still gets the usual names */
    if (paths == NULL) {
        memset(&nopaths, '\0', sizeof(nopaths));
        paths = &nopaths;
    }

    /* validate the specified file is an RPM */
    h = get_header(filename);

    if (h == NULL) {
        warnx(_("*** %s is not a valid RPM"), filename);
        return -1;
    }

    /* make a unique output directory name if we need to */
    dest_dir = extract_dir(h, cwd, output_dir);

    if (dest_dir == NULL) {
        warnx(_("*** unable to set dest_dir"));
        return -1;
    }

    /* create the output directory */
    if (mkdirp(dest_dir, mode) == -1) {
        warnx("mkdirp");
        return -1;
    }

    /*
     * Tag values written to their own file sit next to header.json.
     * With the header going to stdout they keep their usual home.
     */
    if (paths->header == NULL || !strcmp(paths->header, OUTPUT_STDOUT)) {
        header_dir = strdup(dest_dir);
    } else {
        header_dir = dir_name(paths->header);
    }

    if (header_dir == NULL) {
        warnx(_("*** unable to set header_dir"));
        return -1;
    }

    /* read the RPM headers and write them out as JSON */
    if (extract_metadata(filename, dest_dir, header_dir, paths) != 0) {
        return -1;
    }

    /* unpack the RPM payload where the caller asked us to */
    payload_dir = make_payload_dir(dest_dir, paths->payload, mode);

    if (payload_dir == NULL) {
        return -1;
    }

    if (extract_payload(filename, cwd, dest_dir, payload_dir, verbose) != 0) {
        return -1;
    }

    /* clean up */
    free(payload_dir);
    free(dest_dir);
    free(header_dir);
    headerFree(h);

    return 0;
}
