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

/* the widest the ownership and size columns get */
#define MAX_COLUMN_WIDTH 25

/*
 * Work out the widths of the ownership and size columns from the file
 * list in the header, so we read the payload only once.  A %ghost file
 * is in the header but not the payload, so it does not count.  The
 * widths start at ownerlen and sizelen.
 */
static void
column_widths(rpmfiles files, int *ownerlen, int *sizelen)
{
    int l = 0;
    char buf[32];
    rpmfi fi = NULL;

    fi = rpmfilesIter(files, RPMFI_ITER_FWD);

    if (fi == NULL) {
        return;
    }

    while (rpmfiNext(fi) >= 0) {
        if (rpmfiFFlags(fi) & RPMFILE_GHOST) {
            continue;
        }

        l = strlen(str_or_empty(rpmfiFUser(fi))) + strlen(str_or_empty(rpmfiFGroup(fi))) + 1;

        if (l > *ownerlen) {
            *ownerlen = l;
        }

        l = snprintf(buf, sizeof(buf), "%" PRIu64, rpmfiFSize(fi)) + 1;

        if (l > *sizelen) {
            *sizelen = l;
        }
    }

    rpmfiFree(fi);

    /* put a reasonable limit on the column widths */
    if (*ownerlen > MAX_COLUMN_WIDTH) {
        *ownerlen = MAX_COLUMN_WIDTH;
    }

    if (*sizelen > MAX_COLUMN_WIDTH) {
        *sizelen = MAX_COLUMN_WIDTH;
    }

    return;
}

/*
 * Given a path to an RPM package, open and list the payload contents.
 * Returns 0 on success, -1 if the package or its payload cannot be
 * read.
 */
int
list_rpm(const char *rpm)
{
    const char *dn = NULL;
    char *filename = NULL;
    char *escaped = NULL;
    int ownerlen = 14;
    int sizelen = 4;
    char modebuf[BUFSIZ];
    char timebuf[BUFSIZ];
    time_t mtime;
    struct tm filetm;
    char *ownership = NULL;
    char *errstr = NULL;
    int rc = 0;
    int r = 0;
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

    if (compr == NULL) {
        compr = "gzip";
    }

    xasprintf(&rpmio_flags, "r.%s", compr);

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

    /* the widths for the username+groupname and file size come from the header */
    files = rpmfilesNew(NULL, hdr, 0, RPMFI_KEEPHEADER);
    column_widths(files, &ownerlen, &sizelen);

    /* get an iterator over the payload */
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    if (fi == NULL) {
        warnx("*** rpmfiNewArchiveReader returned NULL");
        Fclose(gzdi);
        rpmfilesFree(files);
        headerFree(hdr);
        rpmtsFree(ts);
        return -1;
    }

    /* iterate over every entry in the payload */
    while ((rc = rpmfiNext(fi)) >= 0) {
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
        xasprintf(&ownership, "%s/%s", str_or_empty(rpmfiFUser(fi)), str_or_empty(rpmfiFGroup(fi)));

        /* build and display the line for the payload entry */
        xasprintf(&filename, "%s%-*s%*" PRIu64 " %s .%s%s", modebuf, ownerlen, ownership, sizelen, rpmfiFSize(fi), timebuf, dn, rpmfiBN(fi));
        escaped = escape_name(filename);
        printf("%s\n", escaped);

        free(escaped);
        free(filename);
        free(ownership);
    }

    /* a bad or truncated payload is an error, not the end */
    if (rc != RPMERR_ITER_END) {
        errstr = rpmfileStrerror(rc);
        warnx(_("*** error reading RPM payload: %s"), errstr);
        free(errstr);
        r = -1;
    }

    /* clean up */
    Fclose(gzdi);
    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    return r;
}
