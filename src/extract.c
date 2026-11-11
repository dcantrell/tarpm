/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <rpm/rpmlib.h>
#include <rpm/header.h>
#include <rpm/rpmfiles.h>
#include <rpm/rpmfi.h>
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
        if (!strcmp(path, OUTPUT_STDOUT)) {
            verbose_detail(_("%s to standard output"), name);
        } else {
            verbose_detail("%s", path);
        }

        return write_json_file(data, path);
    }

    file = joinpath(dest_dir, name, NULL);

    if (file == NULL) {
        warn("joinpath");
        return -1;
    }

    /* the stage line already named dest_dir */
    verbose_detail("%s", name);
    r = write_json_file(data, file);

    free(file);

    return r;
}

/*
 * Returns true if the NEVRA can name a directory in the current
 * working directory.  It comes from the package header, so a crafted
 * package could use it to point the extraction somewhere else.  It
 * has to be one path component that is not "." or "..", and it may
 * not hold a control character.
 */
static bool
valid_nevra_dir(const char *nevra)
{
    size_t i = 0;

    if (nevra == NULL || nevra[0] == '\0' || !strcmp(nevra, ".") || !strcmp(nevra, "..")) {
        return false;
    }

    for (i = 0; nevra[i] != '\0'; i++) {
        if (nevra[i] == '/' || iscntrl((unsigned char) nevra[i])) {
            return false;
        }
    }

    return true;
}

/*
 * Returns true if path is a directory below dir.  Both have to be
 * absolute paths with nothing left to resolve.
 */
static bool
below_dir(const char *path, const char *dir)
{
    size_t len = 0;

    if (path == NULL || dir == NULL) {
        return false;
    }

    len = strlen(dir);

    /* the root directory already ends in a slash */
    if (len > 0 && dir[len - 1] == '/') {
        len--;
    }

    if (strncmp(path, dir, len) || path[len] != '/' || path[len + 1] == '\0') {
        return false;
    }

    return true;
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
        return xstrdup(output_dir);
    }

    nevra = get_nevra(h);

    if (nevra == NULL) {
        warnx(_("unable to read NEVRA from RPM header"));
        return NULL;
    }

    if (!valid_nevra_dir(nevra)) {
        warnx(_("*** the NEVRA in the RPM header cannot be used as a directory name, use -O to name one"));
        free(nevra);
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

    if (dir == NULL) {
        warn("abspath");
        return NULL;
    }

    /* we checked the NEVRA above, this is a second line of defense */
    if (!below_dir(dir, cwd)) {
        warnx_escaped(_("*** %s is outside of %s, use -O to name an output directory"), dir, cwd);
        free(dir);
        return NULL;
    }

    return dir;
}

/*
 * rpmbuild leaves the compressor tag off an uncompressed payload and
 * create turns a missing tag in to the default compressor, so we name
 * it in header.json.  rpm reads a missing tag as gzip and an old
 * package can really be that, so we look at the start of the payload,
 * which is where fd sits, and go by the cpio magic.
 */
static void
mark_uncompressed_payload(const int fd, struct json_object *header)
{
    char magic[5];
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        return;
    }

    if (has_tag(tags, RPMTAG_PAYLOADCOMPRESSOR, false)) {
        return;
    }

    if (read(fd, magic, sizeof(magic)) != (ssize_t) sizeof(magic) || memcmp(magic, "07070", sizeof(magic))) {
        return;
    }

    entry = json_object_new_object();
    json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR)));
    json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_STRING_TYPE)));
    json_object_object_add(entry, RPM_ENTRY_VALUE_DESC, json_object_new_string(PAYLOADCOMPRESSOR_NONE));
    json_object_array_add(tags, entry);

    return;
}

/*
 * Read the lead, the signature, and the header out of the RPM and
 * write each one to its JSON file.  Returns 0 if that worked, 1 if
 * we had to keep raw tags in header.json, and -1 if it did not work.
 */
static int
extract_metadata(const char *filename, const char *dest_dir, const char *header_dir, const struct json_paths *paths)
{
    int r = 0;
    int rpmfd = -1;
    size_t n = 0;
    struct json_object *lead = NULL;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    bool raw_tags = false;

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
    verbose_stage(_("Reading the lead"));
    lead = read_lead(rpmfd);

    if (lead == NULL) {
        warnx("read_lead");
        r = -1;
        goto metadata_cleanup;
    }

    verbose_detail(_("lead version %s for a %s package"), str_or_empty(json_object_get_string(json_object_object_get(lead, RPM_LEAD_VERSION))), str_or_empty(json_object_get_string(json_object_object_get(lead, RPM_LEAD_TYPE))));

    /* extract the RPM signature -- the second header (sort of used) */
    verbose_stage(_("Reading the signature header"));
    signature = read_signature(rpmfd);

    if (signature == NULL) {
        warnx("read_signature");
        r = -1;
        goto metadata_cleanup;
    }

    n = key_array_length(signature, RPM_ENTRY_TAGS_DESC);
    verbose_detail("%zu %s", n, N_("tag", "tags", n));

    /* extract the RPM header -- the third header (used) */
    verbose_stage(_("Reading the main header"));
    header = read_header(rpmfd, header_dir, &raw_tags);

    if (header == NULL) {
        warnx("read_header");
        r = -1;
        goto metadata_cleanup;
    }

    /* move the file and verify signatures to the "files" array */
    move_file_signatures(signature, header);

    verbose_counts(header);

    if (raw_tags) {
        verbose_detail(_("some tags stay raw in %s, see the warnings above"), OUTPUT_HEADER);
    }

    /* the payload starts here, right after the main header */
    mark_uncompressed_payload(rpmfd, header);

    /* close the RPM after reading headers */
    if (close(rpmfd) == -1) {
        warn("close");
    }

    rpmfd = -1;

    /*
     * Write out the header metadata.  We make the output directory as
     * each file needs it now, so a write failure means mkdir failed.
     * Warn the user but keep going.
     */
    verbose_stage(_("Writing the metadata"));

    if (write_metadata(lead, dest_dir, paths->lead, OUTPUT_LEAD) != 0) {
        warnx(_("*** unable to write %s"), OUTPUT_LEAD);
        r = -1;
    }

    if (write_metadata(signature, dest_dir, paths->signature, OUTPUT_SIGNATURE) != 0) {
        warnx(_("*** unable to write %s"), OUTPUT_SIGNATURE);
        r = -1;
    }

    if (write_metadata(header, dest_dir, paths->header, OUTPUT_HEADER) != 0) {
        warnx(_("*** unable to write %s"), OUTPUT_HEADER);
        r = -1;
    }

metadata_cleanup:
    if (rpmfd != -1 && close(rpmfd) == -1) {
        warn("close");
    }

    json_object_put(header);
    json_object_put(signature);
    json_object_put(lead);

    if (r == 0 && raw_tags) {
        r = 1;
    }

    return r;
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
        dir = xstrdup(payload_path);
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
 * Unpack the RPM payload in to payload_dir.  libarchive reads a newc
 * payload straight from the RPM, so we try it first.  A header with
 * Longfilesizes means a stripped archive, which only librpm reads, so
 * that one goes to unpack_rpm_payload() right away.  So does a payload
 * libarchive cannot read at all.  Returns 0 if it worked and -1 if it
 * did not.
 */
static int
extract_payload(const char *filename, const char *payload_dir, const bool verbose, const bool stripped)
{
    int r = 0;

    /*
     * An entry that fails after others were written is a real failure
     * and unpacking again would only write it all a second time.
     */
    if (!stripped) {
        verbose_detail(_("reading the cpio archive with libarchive"));
        r = unpack_archive(filename, payload_dir, verbose);

        if (r != UNPACK_UNREADABLE) {
            return r;
        }

        verbose_detail(_("libarchive cannot read this payload, trying librpm"));
    } else {
        verbose_detail(_("reading the stripped cpio archive with librpm"));
    }

    if (unpack_rpm_payload(filename, payload_dir, verbose) != 0) {
        warnx("unpack_rpm_payload");
        return -1;
    }

    return 0;
}

/* Print what the package is and where it goes with -v */
static void
verbose_package(Header h, const char *filename, const char *dest_dir)
{
    char *nevra = NULL;
    const char *kind = _("binary");
    int format = RPM_FORMAT_V4;

    if (!verbose) {
        return;
    }

    if (headerIsSource(h)) {
        kind = _("source");
    }

    if (headerIsEntry(h, RPMTAG_RPMFORMAT_VALUE)) {
        format = RPM_FORMAT_V6;
    }

    nevra = get_nevra(h);
    verbose_stage(_("Extracting %s"), filename);
    verbose_detail(_("%s, a v%d format %s package"), str_or_empty(nevra), format, kind);
    verbose_detail(_("in to %s"), dest_dir);
    free(nevra);
    return;
}

/* Print the payload stage line with -v */
static void
verbose_payload_stage(Header h, const char *payload_dir)
{
    const char *compressor = NULL;

    if (!verbose) {
        return;
    }

    /* rpm reads a missing compressor tag as gzip */
    compressor = headerGetString(h, RPMTAG_PAYLOADCOMPRESSOR);

    if (compressor == NULL) {
        compressor = "gzip";
    }

    verbose_stage(_("Unpacking the %s compressed payload in to %s"), compressor, payload_dir);
    return;
}

/*
 * Print what the payload held with -v.  A %ghost file is in the header
 * but not the payload, so it does not count.  The size is the
 * installed size the header records, which counts hard links once.
 */
static void
verbose_payload_done(Header h)
{
    size_t n = 0;
    char size[HUMAN_SIZE_LEN];
    rpmfiles files = NULL;
    rpmfi fi = NULL;

    if (!verbose) {
        return;
    }

    files = rpmfilesNew(NULL, h, 0, RPMFI_KEEPHEADER);
    fi = rpmfilesIter(files, RPMFI_ITER_FWD);

    while (fi != NULL && rpmfiNext(fi) >= 0) {
        if (rpmfiFFlags(fi) & RPMFILE_GHOST) {
            continue;
        }

        n++;
    }

    rpmfiFree(fi);
    rpmfilesFree(files);

    human_size(headerGetNumber(h, RPMTAG_LONGSIZE), size, sizeof(size));
    verbose_detail(_("%zu %s, %s installed size"), n, N_("payload entry", "payload entries", n), size);
    return;
}

/* Handler for -x mode (extract) */
int
extract_rpm(const char *filename, const char *cwd, const char *output_dir, const struct json_paths *paths, const bool verbose, const bool skip_payload)
{
    char *dest_dir = NULL;
    char *header_dir = NULL;
    char *payload_dir = NULL;
    struct json_paths nopaths;
    Header h;
    int metadata = 0;
    int r = -1;

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
        goto cleanup;
    }

    verbose_package(h, filename, dest_dir);

    /*
     * Tag values written to their own file sit next to header.json.
     * With the header going to stdout they keep their usual home.
     */
    if (paths->header == NULL || !strcmp(paths->header, OUTPUT_STDOUT)) {
        header_dir = xstrdup(dest_dir);
    } else {
        header_dir = xdirname(paths->header);
    }

    if (header_dir == NULL) {
        warnx(_("*** unable to set header_dir"));
        goto cleanup;
    }

    /* read the RPM headers and write them out as JSON */
    metadata = extract_metadata(filename, dest_dir, header_dir, paths);

    if (metadata < 0) {
        goto cleanup;
    }

    /*
     * Unpack the RPM payload where the caller asked us to.  A caller
     * after the metadata alone skips this, so no payload tree is made
     * and the cpio archive is never read.
     */
    if (!skip_payload) {
        payload_dir = make_payload_dir(dest_dir, paths->payload, OUTPUT_DIR_MODE);

        if (payload_dir == NULL) {
            goto cleanup;
        }

        verbose_payload_stage(h, payload_dir);

        if (extract_payload(filename, payload_dir, verbose, headerIsEntry(h, RPMTAG_LONGFILESIZES)) != 0) {
            goto cleanup;
        }

        verbose_payload_done(h);
    } else {
        verbose_stage(_("Skipping the payload"));
    }

    r = metadata;

cleanup:
    free(payload_dir);
    free(dest_dir);
    free(header_dir);
    headerFree(h);

    return r;
}
