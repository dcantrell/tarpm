/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
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
    char *hardlink = NULL;
    rpm_mode_t mode = 0;
    int nlink = 0;
    int rc = 0;
    const char *dn = NULL;
    char *filename = NULL;
    rpm_loff_t left;
    size_t len = 0;
    size_t rd = 0;

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

    /*
     * For at least zstd compression, Fdopen doesn't work correctly
     * with "r.zstd" in certain RPM versions.  Catch it and return
     * NULL here so the caller can handle a fallback approach.
     */
    if (compr && strcmp(compr, "zstd") == 0) {
        Fclose(fdi);
        fdi = NULL;
        goto cleanup;
    }

    xasprintf(&rpmio_flags, "r.%s", compr ? compr : "gzip");

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

    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE);

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

    /* the RPM payload is sort of cpio */
    if (archive_write_set_format_cpio_newc(archive) != ARCHIVE_OK) {
        warnx("*** archive_write_set_format_cpio_newc: %s", archive_error_string(archive));
        goto cleanup;
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

        if (nlink > 1) {
            if (rpmfiArchiveHasContent(fi)) {
                free(hardlink);
                hardlink = strdup(archive_entry_pathname(entry));

                if (hardlink == NULL) {
                    err(EXIT_FAILURE, "strdup");
                }
            } else if (hardlink != NULL) {
                archive_entry_set_hardlink(entry, hardlink);
            } else {
                warnx(_("*** hardlink target missing for %s"), archive_entry_pathname(entry));
            }
        }

        archive_write_header(archive, entry);

        if (S_ISREG(mode) && (nlink == 1 || rpmfiArchiveHasContent(fi))) {
            left = rpmfiFSize(fi);

            while (left) {
                len = (left > BUFSIZ ? BUFSIZ : left);
                rd = rpmfiArchiveRead(fi, buf, len);

                if (rd == len) {
                    archive_write_data(archive, buf, len);
                } else {
                    warnx(_("*** error reading file from RPM payload"));
                    break;
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

    free(hardlink);
    free(buf);

    if (entry) {
        archive_entry_free(entry);
    }

    if (archive) {
        archive_write_close(archive);
        archive_write_free(archive);
    }

    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

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
