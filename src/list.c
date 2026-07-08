/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <string.h>
#include <assert.h>
#include <err.h>
#include <rpm/rpmlib.h>
#include <rpm/header.h>
#include <rpm/rpmts.h>

#include "tarpm.h"

/*
 * Given a path to an RPM package, open and list the payload contents.
 */
void
list_rpm(const char *rpm)
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
    char *buf = NULL;
    char *hardlink = NULL;
//    rpm_mode_t mode = 0;
//    int nlink = 0;
    int rc = 0;
    const char *dn = NULL;
    char *filename = NULL;

    if (rpm == NULL) {
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
        fdi = NULL;
        goto cleanup;
    }

    /* determine how to read the payload */
    compr = headerGetString(hdr, RPMTAG_PAYLOADCOMPRESSOR);
    xasprintf(&rpmio_flags, "r.%s", compr ? compr : "gzip");
    assert(rpmio_flags != NULL);

    /* open the payload */
    gzdi = Fdopen(fdi, rpmio_flags);
    free(rpmio_flags);

    if (gzdi == NULL) {
        warnx("*** Fdopen: %s", Fstrerror(fdi));
        goto cleanup;
    }

    files = rpmfilesNew(NULL, hdr, 0, RPMFI_KEEPHEADER);
    fi = rpmfiNewArchiveReader(gzdi, files, RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    /* iterate over every entry in the payload */
    while (rc >= 0) {
        rc = rpmfiNext(fi);

        if (rc == RPMERR_ITER_END) {
            break;
        }

//        mode = rpmfiFMode(fi);
        dn = rpmfiDN(fi);

        if (!strcmp(dn, "")) {
            dn = "/";
        }

        xasprintf(&filename, ".%s%s", dn, rpmfiBN(fi));
        assert(filename != NULL);

/* XXX - need to get 'tar -tvf' style output */

        printf("%s\n", filename);
        free(filename);

//        archive_entry_set_size(entry, rpmfiFSize(fi));
//        archive_entry_set_filetype(entry, mode & S_IFMT);
//        archive_entry_set_perm(entry, mode);
//        archive_entry_set_uname(entry, rpmfiFUser(fi));
//        archive_entry_set_gname(entry, rpmfiFGroup(fi));
//        archive_entry_set_rdev(entry, rpmfiFRdev(fi));
//        archive_entry_set_mtime(entry, rpmfiFMtime(fi), 0);
//
//        if (S_ISLNK(mode)) {
//            archive_entry_set_symlink(entry, rpmfiFLink(fi));
//        }
//
//        if (nlink > 1) {
//            if (rpmfiArchiveHasContent(fi)) {
//                free(hardlink);
//                hardlink = strdup(archive_entry_pathname(entry));
//                assert(hardlink != NULL);
//            } else {
//                archive_entry_set_hardlink(entry, hardlink);
//            }
//        }
    }

cleanup:
    if (gzdi) {
        Fclose(gzdi);
    } else {
        Fclose(fdi);
    }

    free(hardlink);
    free(buf);

    rpmfilesFree(files);
    rpmfiFree(fi);
    headerFree(hdr);
    rpmtsFree(ts);

    return;
}
