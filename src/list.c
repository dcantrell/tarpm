/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <inttypes.h>
#include <err.h>
#include <rpm/rpmlib.h>
#include <rpm/header.h>
#include <rpm/rpmts.h>
#include "tarpm.h"

static rpmVSFlags vsflags = RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES | RPMVSF_NOHDRCHK;

#define COLUMN_OWNERSHIP 0
#define COLUMN_SIZE      1

static int
max_column_strlen(const char *rpm, const int standard, const int column)
{
    int r = standard;
    int l = 0;
    char *buf = NULL;
    int rc = 0;
    rpmts ts;
    Header hdr;
    const char *compr = NULL;
    char *rpmio_flags = NULL;
    FD_t fdi = NULL;
    FD_t gzdi = NULL;
    rpmfiles files = NULL;
    rpmfi fi = NULL;

    if (rpm == NULL) {
        return -1;
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
        rpmtsFree(ts);
        return -1;
    }

    /* determine how to read the payload */
    compr = headerGetString(hdr, RPMTAG_PAYLOADCOMPRESSOR);
    xasprintf(&rpmio_flags, "r.%s", compr ? compr : "gzip");

    /* open the payload */
    gzdi = Fdopen(fdi, rpmio_flags);
    free(rpmio_flags);

    if (gzdi == NULL) {
        warnx("*** Fdopen: %s", Fstrerror(fdi));
        Fclose(fdi);
        headerFree(hdr);
        rpmtsFree(ts);
        return -1;
    }

    /* get an iterator over the payload */
    files = rpmfilesNew(NULL, hdr, 0, RPMFI_KEEPHEADER);
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    if (fi == NULL) {
        Fclose(gzdi);
        rpmfilesFree(files);
        headerFree(hdr);
        rpmtsFree(ts);
        return -1;
    }

    /* find the maximum column width we need */
    while (rpmfiNext(fi) >= 0) {
        if (column == COLUMN_OWNERSHIP) {
            l = strlen(rpmfiFUser(fi)) + strlen(rpmfiFGroup(fi)) + 1;
        } else if (column == COLUMN_SIZE) {
            xasprintf(&buf, "%" PRIu64 "", rpmfiFSize(fi));
            l = strlen(buf) + 1;
            free(buf);
        }

        if (l > r) {
            r = l;
        }
    }

    /* put a reasonable limit on the column width */
    if (r > 25) {
        r = 25;
    }

    /* clean up */
    Fclose(gzdi);
    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    return r;
}

/*
 * Given a path to an RPM package, open and list the payload contents.
 */
void
list_rpm(const char *rpm)
{
    const char *dn = NULL;
    char *filename = NULL;
    int ownerlen = 0;
    int sizelen = 0;
    char modebuf[BUFSIZ];
    char timebuf[BUFSIZ];
    time_t mtime;
    struct tm filetm;
    char *ownership = NULL;
    int rc = 0;
    rpmts ts;
    Header hdr;
    const char *compr = NULL;
    char *rpmio_flags = NULL;
    FD_t fdi = NULL;
    FD_t gzdi = NULL;
    rpmfiles files = NULL;
    rpmfi fi = NULL;

    if (rpm == NULL) {
        return;
    }

    /* get the max string lengths for the username+groupname and file size */
    ownerlen = max_column_strlen(rpm, 14, COLUMN_OWNERSHIP);

    if (ownerlen == -1) {
        warnx("max_column_strlen: ownership");
        return;
    }

    sizelen = max_column_strlen(rpm, 4, COLUMN_SIZE);

    if (sizelen == -1) {
        warnx("max_column_strlen: size");
        return;
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
        rpmtsFree(ts);
        return;
    }

    /* determine how to read the payload */
    compr = headerGetString(hdr, RPMTAG_PAYLOADCOMPRESSOR);
    xasprintf(&rpmio_flags, "r.%s", compr ? compr : "gzip");

    /* open the payload */
    gzdi = Fdopen(fdi, rpmio_flags);
    free(rpmio_flags);

    if (gzdi == NULL) {
        warnx("*** Fdopen: %s", Fstrerror(fdi));
        Fclose(fdi);
        headerFree(hdr);
        rpmtsFree(ts);
        return;
    }

    /* get an iterator over the payload */
    files = rpmfilesNew(NULL, hdr, 0, RPMFI_KEEPHEADER);
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    if (fi == NULL) {
        Fclose(gzdi);
        rpmfilesFree(files);
        headerFree(hdr);
        rpmtsFree(ts);
        return;
    }

    /* iterate over every entry in the payload */
    while (rpmfiNext(fi) >= 0) {
        dn = rpmfiDN(fi);

        if (!strcmp(dn, "")) {
            dn = "/";
        }

        /* format the rpm_mode_t of this file entry to ls(1) & tar(1) style output */
        strmode(rpmfiFMode(fi), modebuf);

        /* format the rpm_time_t of this file entry to ls(1) & tar(1) style output */
        mtime = rpmfiFMtime(fi);

        if (gmtime_r(&mtime, &filetm) == NULL) {
            warn("gmtime_r");
            continue;
        }

        if (strftime(timebuf, BUFSIZ, "%F %R", &filetm) == 0) {
            warn("strftime");
            continue;
        }

        /* ownership formatted string */
        xasprintf(&ownership, "%s/%s", rpmfiFUser(fi), rpmfiFGroup(fi));

        /* build and display the line for the payload entry */
        xasprintf(&filename, "%s%-*s%*" PRIu64 " %s .%s%s", modebuf, ownerlen, ownership, sizelen, rpmfiFSize(fi), timebuf, dn, rpmfiBN(fi));
        printf("%s\n", filename);

        free(filename);
        free(ownership);
    }

    /* clean up */
    Fclose(gzdi);
    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    return;
}
