/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file
 * @brief The librpm payload reader and a few header helpers.
 *
 * librpm reads the stripped cpio archive libarchive cannot, which is
 * every v6 format payload and a v4 format one with Longfilesizes.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>
#include <archive.h>
#include <archive_entry.h>
#include <rpm/rpmlib.h>
#include <rpm/header.h>
#include <rpm/rpmts.h>

#include "tarpm.h"

/* Returns true if file index fx is in the hard link set of the current entry */
static bool
in_link_set(rpmfi fi, const int fx)
{
    uint32_t i = 0;
    uint32_t n = 0;
    const int *links = NULL;

    n = rpmfiFLinks(fi, &links);

    for (i = 0; i < n; i++) {
        if (links[i] == fx) {
            return true;
        }
    }

    return false;
}

/*
 * Count the entries the payload should hand us.  That is every file
 * in the header but a %ghost, which is in the header and not the
 * payload.
 */
static size_t
payload_entries(rpmfiles files)
{
    size_t n = 0;
    rpmfi fi = NULL;

    fi = rpmfilesIter(files, RPMFI_ITER_FWD);

    if (fi == NULL) {
        return 0;
    }

    while (rpmfiNext(fi) >= 0) {
        if (rpmfiFFlags(fi) & RPMFILE_GHOST) {
            continue;
        }

        n++;
    }

    rpmfiFree(fi);
    return n;
}

/*
 * Write the data of a regular file entry to disk.  We read all of it
 * even when a write fails so the iterator stays in step with the
 * payload.  Returns 0 on success, -1 when the write failed and -2
 * when the payload could not be read, which ends the unpack.
 */
static int
write_payload_file(rpmfi fi, struct archive *output, char *buf, const bool writing)
{
    int r = 0;
    bool ok = writing;
    rpm_loff_t left = 0;
    size_t len = 0;
    ssize_t got = 0;

    left = rpmfiFSize(fi);

    while (left) {
        if (left > BUFSIZ) {
            len = BUFSIZ;
        } else {
            len = left;
        }

        /* rpm 4.14 returns size_t here and later releases ssize_t */
        got = rpmfiArchiveRead(fi, buf, len);

        if (got != (ssize_t) len) {
            warnx(_("*** error reading file from RPM payload"));
            return -2;
        }

        if (ok && archive_write_data(output, buf, len) != (ssize_t) len) {
            warnx_escaped("archive_write_data: %s", archive_error_string(output));
            ok = false;
            r = -1;
        }

        left -= len;
    }

    return r;
}

/*
 * Unpack the payload of an RPM in to dest with librpm.  This is the
 * reader for the stripped cpio archive libarchive cannot read, which
 * is every v6 format payload and a v4 format one with Longfilesizes.
 * The entries go straight to the same libarchive disk writer
 * unpack_archive() uses, so nothing is written anywhere else.  An
 * entry that fails is skipped like unpack_archive() does.  Returns 0
 * if every entry went in and -1 otherwise.
 *
 * A lot of this is adapted from rpm2archive.c from the rpm sources.
 */
int
unpack_rpm_payload(const char *rpm, const char *dest, const bool verbose)
{
    rpmts ts;
    rpmVSFlags vsflags = RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES | RPMVSF_NOHDRCHK;
    Header hdr = NULL;
    FD_t fdi = NULL;
    FD_t gzdi = NULL;
    const char *compr = NULL;
    char *rpmio_flags = NULL;
    rpmfiles files = NULL;
    rpmfi fi = NULL;
    struct archive *output = NULL;
    struct archive_entry *entry = NULL;
    char cwd[PATH_MAX + 1];
    char *buf = NULL;
    size_t seen = 0;
    size_t expected = 0;
    size_t missing = 0;
    rpm_mode_t mode = 0;
    int nlink = 0;
    int rc = 0;
    int r = 0;
    int ret = -1;
    bool writing = false;
    bool changed_dir = false;
    const char *dn = NULL;
    char *filename = NULL;
    char *hardlink = NULL;
    int hardlinkfx = -1;
    char *errstr = NULL;

    if (rpm == NULL || dest == NULL) {
        return -1;
    }

    memset(cwd, '\0', sizeof(cwd));

    /* create librpm widgets */
    ts = rpmtsCreate();
    rpmtsSetVSFlags(ts, vsflags);

    /* open the package */
    fdi = Fopen(rpm, "r.ufdio");

    if (fdi == NULL || Ferror(fdi)) {
        warnx(_("Fopen failed for %s: %s"), rpm, Fstrerror(fdi));

        if (fdi) {
            Fclose(fdi);
        }

        fdi = NULL;
        goto cleanup;
    }

    rc = rpmReadPackageFile(ts, fdi, rpm, &hdr);

    /* librpm says why, errno here is left over from something else */
    if (rc == RPMRC_NOTFOUND || rc == RPMRC_FAIL) {
        warnx(_("*** %s is not a valid RPM"), rpm);
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

    expected = payload_entries(files);

    /* a hardlink names an earlier entry, so the content comes first */
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    if (fi == NULL) {
        warnx("*** rpmfiNewArchiveReader returned NULL");
        goto cleanup;
    }

    /* the entry paths are relative, so we work from dest */
    if (getcwd(cwd, PATH_MAX) == NULL) {
        warn("getcwd");
        goto cleanup;
    }

    if (chdir(dest) != 0) {
        warn("chdir");
        goto cleanup;
    }

    changed_dir = true;
    ret = 0;
    output = new_disk_writer();
    entry = archive_entry_new();
    buf = xalloc(BUFSIZ);

    /* iterate over every entry in the payload */
    while (true) {
        rc = rpmfiNext(fi);

        if (rc == RPMERR_ITER_END) {
            /*
             * A hard link set the payload carries out of order stops
             * the content first iterator here rather than at a
             * missing file, so a short count is all we get to tell us
             * the rest of the payload never arrived.
             */
            if (seen < expected) {
                missing = expected - seen;
                warnx(_("*** the RPM payload ended early with %zu %s missing"), missing, N_("entry", "entries", missing));
                ret = -1;
            }

            break;
        }

        /* a bad or truncated payload is an error, not the end */
        if (rc < 0) {
            errstr = rpmfileStrerror(rc);
            warnx(_("*** error reading RPM payload: %s"), errstr);
            free(errstr);
            ret = -1;
            break;
        }

        seen++;
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
                hardlink = xstrdup(archive_entry_pathname(entry));
                hardlinkfx = rpmfiFX(fi);
            } else if (hardlink != NULL && in_link_set(fi, hardlinkfx)) {
                archive_entry_set_hardlink(entry, hardlink);
                archive_entry_set_size(entry, 0);
            } else {
                /* an empty file of the recorded size would be wrong */
                warnx_escaped(_("*** %s is a hard link with no earlier link to point at, skipping it"), archive_entry_pathname(entry));
                ret = -1;
                continue;
            }
        }

        if (verbose) {
            print_entry_path(entry);
        }

        restrict_mode(entry);
        writing = true;

        if (archive_write_header(output, entry) != ARCHIVE_OK) {
            warnx_escaped("archive_write_header: %s", archive_error_string(output));
            writing = false;
            ret = -1;
        }

        if (S_ISREG(mode) && (nlink == 1 || rpmfiArchiveHasContent(fi))) {
            r = write_payload_file(fi, output, buf, writing);

            if (r == -2) {
                ret = -1;
                break;
            }

            if (r != 0) {
                ret = -1;
            }
        }

        r = archive_write_finish_entry(output);

        if (r < ARCHIVE_WARN) {
            ret = -1;
        } else if (r != ARCHIVE_OK) {
            warnx_escaped("archive_write_finish_entry: %s", archive_error_string(output));
        }
    }

    /* closing sets the directory modes and times libarchive held back */
    r = archive_write_close(output);

    if (r != ARCHIVE_OK) {
        warnx_escaped("archive_write_close: %s", archive_error_string(output));

        if (r < ARCHIVE_WARN) {
            ret = -1;
        }
    }

cleanup:
    if (gzdi) {
        Fclose(gzdi);
    } else if (fdi) {
        Fclose(fdi);
    }

    free(buf);
    free(hardlink);

    if (entry) {
        archive_entry_free(entry);
    }

    if (output) {
        archive_write_free(output);
    }

    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    /* change back to original directory */
    if (changed_dir && chdir(cwd) != 0) {
        warn("chdir");
        ret = -1;
    }

    return ret;
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
    const char *str = NULL;
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

    /* a tag that is not a string has no string value */
    str = rpmtdGetString(td);

    if (str != NULL) {
        val = xstrdup(str);
    }

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
