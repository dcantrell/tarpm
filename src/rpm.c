/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <locale.h>
#include <archive.h>
#include <archive_entry.h>
#include <rpm/rpmlib.h>
#include <rpm/header.h>
#include <rpm/rpmts.h>

#include "tarpm.h"

/*
 * Given a path to an RPM package, convert the payload to a tar file
 * for later use with extract_rpm().  This happens in cases where
 * libarchive cannot detect the cpio stream in an opened RPM file.
 * The caller must free the returned path string.
 *
 * A lot of this is adapted from rpm2archive.c from the rpm sources.
 */
char *
convert_payload(const char *rpm)
{
    char *payload = NULL;
    rpmts ts;
    rpmVSFlags vsflags = RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES | RPMVSF_NOHDRCHK;
    Header hdr = NULL;
    FD_t fdi = NULL;
    FD_t gzdi = NULL;
    const char *compr = NULL;
    char *rpmio_flags = NULL;
    rpmfiles files = NULL;
    rpmfi fi = NULL;
    struct archive *archive = NULL;
    struct archive_entry *entry = NULL;
    char *buf = NULL;
    rpm_mode_t mode = 0;
    int nlink = 0;
    int rc = 0;
    const char *dn = NULL;
    char *filename = NULL;
    char *hardlink = NULL;
    char *ctype = NULL;
    char *errstr = NULL;
    rpm_loff_t left;
    size_t len = 0;
    size_t rd = 0;
    bool failed = false;

    if (rpm == NULL) {
        return NULL;
    }

    /* create librpm widgets */
    ts = rpmtsCreate();
    rpmtsSetVSFlags(ts, vsflags);

    /* open the package */
    fdi = Fopen(rpm, "r.ufdio");
    rc = rpmReadPackageFile(ts, fdi, COMMAND_NAME, &hdr);

    if (rc == RPMRC_NOTFOUND || rc == RPMRC_FAIL) {
        warn("*** rpmReadPackageFile");
        Fclose(fdi);
        fdi = NULL;
        goto cleanup;
    }

    /* determine how to read the payload */
    compr = headerGetString(hdr, RPMTAG_PAYLOADCOMPRESSOR);

    /* default to "gzip" for compr */
    if (compr == NULL) {
        compr = "gzip";
    }

    /* build the flags for librpm */
    xasprintf(&rpmio_flags, "r.%s", compr);

    /* open the payload */
    gzdi = Fdopen(fdi, rpmio_flags);
    free(rpmio_flags);

    if (gzdi == NULL) {
        warnx("*** Fdopen: %s", Fstrerror(fdi));
        goto cleanup;
    }

    files = rpmfilesNew(NULL, hdr, 0, RPMFI_KEEPHEADER);

    if (files == NULL) {
        warnx("*** rpmfilesNew returned NULL");
        goto cleanup;
    }

    /* a tar hardlink names an earlier entry, so the content comes first */
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    if (fi == NULL) {
        warnx("*** rpmfiNewArchiveReader returned NULL");
        goto cleanup;
    }

    /* create a new archive with the payload data */
    archive = archive_write_new();

    /*
     * add the compression filter based on the type indicated in
     * the header
     */
    if (!strcmp(compr, "gzip")) {
        if (archive_write_add_filter_gzip(archive) != ARCHIVE_OK) {
            warnx("*** archive_write_add_filter_gzip: %s", archive_error_string(archive));
            goto cleanup;
        }
    } else if (!strcmp(compr, "bzip2")) {
        if (archive_write_add_filter_bzip2(archive) != ARCHIVE_OK) {
            warnx("*** archive_write_add_filter_bzip2: %s", archive_error_string(archive));
            goto cleanup;
        }
    } else if (!strcmp(compr, "xz")) {
        if (archive_write_add_filter_xz(archive) != ARCHIVE_OK) {
            warnx("*** archive_write_add_filter_xz: %s", archive_error_string(archive));
            goto cleanup;
        }
    } else if (!strcmp(compr, "lzma")) {
        if (archive_write_add_filter_lzma(archive) != ARCHIVE_OK) {
            warnx("*** archive_write_add_filter_lzma: %s", archive_error_string(archive));
            goto cleanup;
        }
    } else if (!strcmp(compr, "zstd")) {
        if (archive_write_add_filter_zstd(archive) != ARCHIVE_OK) {
            warnx("*** archive_write_add_filter_zstd: %s", archive_error_string(archive));
            goto cleanup;
        }
    }

    /*
     * Write pax the way rpm2archive does.  newc cannot hold a file of
     * 4 GiB or more, which a package carrying Longfilesizes can have.
     * The paths are UTF-8.
     */
    if (archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK) {
        warnx("*** archive_write_set_format_pax_restricted: %s", archive_error_string(archive));
        goto cleanup;
    }

    ctype = setlocale(LC_CTYPE, NULL);
    ctype = (ctype == NULL) ? NULL : strdup(ctype);
    setlocale(LC_CTYPE, "C");
    (void) archive_write_set_options(archive, "hdrcharset=UTF-8");

    if (ctype != NULL) {
        setlocale(LC_CTYPE, ctype);
        free(ctype);
    }

    xasprintf(&payload, "%s.tar", rpm);

    if (archive_write_open_filename(archive, payload) != ARCHIVE_OK) {
        warnx("*** archive_write_open_filename: %s", archive_error_string(archive));
        free(payload);
        payload = NULL;
        goto cleanup;
    }

    /* iterate over every entry in the payload */
    entry = archive_entry_new();
    buf = xalloc(BUFSIZ);

    while (rc >= 0) {
        rc = rpmfiNext(fi);

        if (rc == RPMERR_ITER_END) {
            break;
        }

        /* a bad or truncated payload is an error, not the end */
        if (rc < 0) {
            errstr = rpmfileStrerror(rc);
            warnx(_("*** error reading RPM payload: %s"), errstr);
            free(errstr);
            failed = true;
            goto cleanup;
        }

        mode = rpmfiFMode(fi);
        nlink = rpmfiFNlink(fi);

        archive_entry_clear(entry);
        dn = rpmfiDN(fi);

        if (dn == NULL || !strcmp(dn, "")) {
            dn = "/";
        }

        xasprintf(&filename, ".%s%s", dn, rpmfiBN(fi));
        archive_entry_copy_pathname(entry, filename);
        free(filename);

        archive_entry_set_size(entry, rpmfiFSize(fi));
        archive_entry_set_filetype(entry, mode & S_IFMT);
        archive_entry_set_perm(entry, mode);
        archive_entry_set_uname(entry, rpmfiFUser(fi));
        archive_entry_set_gname(entry, rpmfiFGroup(fi));
        archive_entry_set_rdev(entry, rpmfiFRdev(fi));
        archive_entry_set_mtime(entry, rpmfiFMtime(fi), 0);

        if (S_ISLNK(mode)) {
            archive_entry_set_symlink(entry, rpmfiFLink(fi));
        }

        archive_entry_set_nlink(entry, nlink);

        /* the first link carries the content and the rest name it */
        if (nlink > 1) {
            if (rpmfiArchiveHasContent(fi)) {
                free(hardlink);
                hardlink = strdup(archive_entry_pathname(entry));
            } else {
                archive_entry_set_hardlink(entry, hardlink);
                archive_entry_set_size(entry, 0);
            }
        }

        if (archive_write_header(archive, entry) != ARCHIVE_OK) {
            warnx(_("archive_write_header: %s"), archive_error_string(archive));
            failed = true;
            goto cleanup;
        }

        if (S_ISREG(mode) && (nlink == 1 || rpmfiArchiveHasContent(fi))) {
            left = rpmfiFSize(fi);

            while (left) {
                len = (left > BUFSIZ ? BUFSIZ : left);
                rd = rpmfiArchiveRead(fi, buf, len);

                if (rd == len) {
                    if (archive_write_data(archive, buf, len) != (ssize_t) len) {
                        warnx(_("archive_write_data: %s"), archive_error_string(archive));
                        failed = true;
                        goto cleanup;
                    }
                } else {
                    warnx(_("*** error reading file from RPM payload"));
                    failed = true;
                    goto cleanup;
                }

                left -= len;
            }
        }
    }

cleanup:
    if (gzdi) {
        Fclose(gzdi);
    } else {
        Fclose(fdi);
    }

    free(buf);
    free(hardlink);

    if (entry) {
        archive_entry_free(entry);
    }

    if (archive) {
        /* the tar file is not done until the close flushes it */
        if (archive_write_close(archive) != ARCHIVE_OK && payload != NULL && !failed) {
            warnx(_("archive_write_close: %s"), archive_error_string(archive));
            failed = true;
        }

        archive_write_free(archive);
    }

    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    if (failed) {
        if (unlink(payload) == -1) {
            warn("unlink");
        }

        free(payload);
        payload = NULL;
    }

    return payload;
}

/*
 * Return an RPM header struct for the given package filename.
 */
Header
get_header(const char *pkg)
{
    Header h;
    rpmts ts;
    FD_t fd;
    rpmRC result;

    if (pkg == NULL) {
        return NULL;
    }

    fd = Fopen(pkg, "r.ufdio");

    if (fd == NULL || Ferror(fd)) {
        warnx(_("Fopen failed for %s: %s"), pkg, Fstrerror(fd));

        if (fd) {
            Fclose(fd);
        }

        return NULL;
    }

    ts = rpmtsCreate();
    rpmtsSetVSFlags(ts, _RPMVSF_NODIGESTS | _RPMVSF_NOSIGNATURES);
    result = rpmReadPackageFile(ts, fd, pkg, &h);
    rpmtsFree(ts);
    Fclose(fd);

    if (result != RPMRC_OK) {
        return NULL;
    }

    return h;
}

/*
 * Get and return the named RPM header tag as a string.
 */
char *
get_rpmtag_str(Header h, rpmTagVal tag)
{
    char *val = NULL;
    rpmtd td = NULL;
    rpm_count_t td_size;

    /* no header means no tag value */
    if (h == NULL) {
        return NULL;
    }

    td = rpmtdNew();

    if (td == NULL) {
        return NULL;
    }

    /* NOTE: this function returns 1 for success, not RPMRC_OK */
    if (headerGet(h, tag, td, HEADERGET_MINMEM | HEADERGET_EXT) != 1) {
        goto val_cleanup;
    }

    td_size = rpmtdCount(td);

    if (td_size != 1) {
        goto val_cleanup;
    }

    val = strdup(rpmtdGetString(td));

val_cleanup:
    rpmtdFree(td);
    return val;
}

/*
 * Returns the RPMTAG_ARCH or "src" if it's a source RPM.
 * NOTE: Do not free() what this function returns.
 */
const char *
get_header_arch(Header h)
{
    if (h == NULL) {
        return NULL;
    }

    if (headerIsSource(h)) {
        return SRPM_ARCH_NAME;
    } else {
        return headerGetString(h, RPMTAG_ARCH);
    }
}

/*
 * Get the RPMTAG_NEVR extension tag.
 * NOTE: Caller must free this result.
 */
char *
get_nevr(Header h)
{
    return get_rpmtag_str(h, RPMTAG_NEVR);
}

/*
 * Get the RPMTAG_NEVRA extension tag equivalent.  Do not use
 * RPMTAG_NEVRA directly here because on source RPMs, the "A" part of
 * NEVRA will have been written at rpmbuild time and does not report
 * "source" as the architecture but rather the architecture that you
 * ran rpmbuild on.
 * NOTE: Caller must free this result.
 */
char *
get_nevra(Header h)
{
    char *r = NULL;
    const char *a = NULL;

    if (h == NULL) {
        return NULL;
    }

    r = get_nevr(h);

    if (r == NULL) {
        return NULL;
    }

    a = get_header_arch(h);

    if (a) {
        return strappend(r, ".", a, NULL);
    }

    return r;
}
