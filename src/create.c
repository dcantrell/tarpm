/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <ftw.h>
#include <err.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <json_object.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>
#include <rpm/rpmfiles.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <zstd.h>

#include "tarpm.h"

static struct archive *payload = NULL;

static void
free_header(struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo)
{
    if (hdr) {
        free(hdr);
        hdr = NULL;
    }

    if (hdrinfo) {
        if (hdrinfo->estart) {
            free(hdrinfo->estart);
            hdrinfo->estart = NULL;
        }

        if (hdrinfo->datastart) {
            free(hdrinfo->datastart);
            hdrinfo->datastart = NULL;
        }

        free(hdrinfo);
        hdrinfo = NULL;
    }

    return;
}

/* Helper for create_rpm() that writes header data to the RPM */
static int
write_header(FILE *rpm, struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo, bool is_signature, struct json_object *data)
{
    uint32_t n = 0;
    uint32_t nentries = 0;
    uint32_t nbytes = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    uint8_t padding[8] = {0};
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int32_t trailer_offset = 0;
    int r = 0;

    if (rpm == NULL || hdr == NULL || hdrinfo == NULL) {
        return 0;
    }

    if (fwrite(hdr, sizeof(*hdr), 1, rpm) != 1) {
        warn("fwrite");
        r = -1;
    }

    /* write the signature index entries */
    nentries = ntohl(hdr->nentries);

    if (fwrite(hdrinfo->estart, sizeof(struct rpmhdrentry), nentries, rpm) != nentries) {
        warn("fwrite");
        r = -1;
    }

    /* write the signature data */
    nbytes = ntohl(hdr->nbytes);
    n = nbytes;

    /*
     * in HEADER_SIGNATURES or HEADER_IMMUTABLE, actual data is 16
     * bytes less, because the trailer is counted in nbytes but
     * not written in the main data section
     */
    if (has_trailer(nentries, hdrinfo->estart)) {
        n -= 16;
        r = get_trailer_data(data, &trailer_data, &trailer_size);

        /*
         * The region trailer records the size of the index entries
         * it covers as a negative offset.  The trailer from the JSON
         * still describes the old header, so we work the offset out
         * again from the entries we really wrote.  That leaves out
         * read-only tags like the signatures on a signed package.
         */
        if (r == 0 && trailer_size == 16) {
            trailer_offset = htonl(-((int32_t) (nentries * sizeof(struct rpmhdrentry))));
            memcpy(trailer_data + (2 * sizeof(int32_t)), &trailer_offset, sizeof(trailer_offset));
        }
    }

    if (fwrite(hdrinfo->datastart, 1, n, rpm) != n) {
        warn("fwrite");
        r = -1;
    }

    /* write the trailer if present (for both signature and header sections) */
    if (r == 0 && trailer_size == 16) {
        if (fwrite(trailer_data, 1, trailer_size, rpm) != trailer_size) {
            warn("fwrite");
            r = -1;
        }
    }

    free(trailer_data);

    /* write padding after signature data to align to 8-byte boundary */
    if (is_signature) {
        /* padding is based on index entries + full nbytes (including trailer) */
        hlen = nentries * sizeof(struct rpmhdrentry) + nbytes;
        padlen = (8 - (hlen % 8)) % 8;

        if (padlen > 0) {
            if (fwrite(padding, 1, padlen, rpm) != padlen) {
                warn("fwrite");
                r = -1;
            }
        }
    }

    return r;
}

/*
 * Build the name a file carries in the cpio payload.  Binary packages
 * prefix every path with "./".  Source RPMs have no directory prefix
 * at all.  rpmbuild writes their bare filenames.  Caller must free
 * the returned string.
 */
static char *
payload_path(const char *dirname, const char *basename)
{
    char *path = NULL;

    if (dirname == NULL || basename == NULL) {
        return NULL;
    }

    if (dirname[0] == '\0') {
        path = strdup(basename);
    } else if (dirname[0] == '/') {
        xasprintf(&path, ".%s%s", dirname, basename);
    } else {
        xasprintf(&path, "./%s%s", dirname, basename);
    }

    return path;
}

/*
 * Helper function used to build the RPM payload.  We use libarchive,
 * but the payload has to follow the metadata in the header rather
 * than walking the filesystem tree.
 */
static int
add_file_to_payload(const char *payload_dir, const struct file_params *params)
{
    char *relative_path = NULL;
    char *full_path = NULL;
    char *file_path = NULL;
    struct archive_entry *entry = NULL;
    int fd = -1;
    ssize_t len = 0;
    char buf[BUFSIZ];

    if (payload_dir == NULL || params == NULL) {
        return -1;
    }

    /* the relative path is where this file will live when installed */
    relative_path = joinpath(params->dirname, params->basename, NULL);

    /* this is the actual location of the file going in the payload */
    file_path = joinpath(payload_dir, relative_path, NULL);

    /* start a new entry */
    entry = archive_entry_new();

    full_path = payload_path(params->dirname, params->basename);
    archive_entry_set_pathname(entry, full_path);

    /* set file type and related metadata */
    if (S_ISREG(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFREG);

        if (params->hardlink != NULL) {
            /* hardlinks have size=0 and link to the first occurrence */
            archive_entry_set_size(entry, 0);
            archive_entry_set_hardlink(entry, params->hardlink);
        } else {
            archive_entry_set_size(entry, params->size);
        }
    } else if (S_ISDIR(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFDIR);
        archive_entry_set_size(entry, 0);
    } else if (S_ISLNK(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFLNK);
        archive_entry_set_size(entry, 0);

        if (params->linkto != NULL && params->linkto[0] != '\0') {
            archive_entry_set_symlink(entry, params->linkto);
        }
    } else if (S_ISCHR(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFCHR);
        archive_entry_set_size(entry, 0);
        archive_entry_set_rdev(entry, params->rdev);
    } else if (S_ISBLK(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFBLK);
        archive_entry_set_size(entry, 0);
        archive_entry_set_rdev(entry, params->rdev);
    } else if (S_ISFIFO(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFIFO);
        archive_entry_set_size(entry, 0);
    } else if (S_ISSOCK(params->mode)) {
        archive_entry_set_filetype(entry, AE_IFSOCK);
        archive_entry_set_size(entry, 0);
    }

    /* set access permissions, mtime, uid, and gid */
    archive_entry_set_perm(entry, params->mode & ACCESSPERMS);
    archive_entry_set_mtime(entry, params->mtime, 0);
    archive_entry_set_uid(entry, params->uid);
    archive_entry_set_gid(entry, params->gid);

    /* set inode number and link count if provided (needed for hardlinks) */
    if (params->inode > 0) {
        archive_entry_set_ino64(entry, params->inode);
    }

    if (params->nlink > 0) {
        archive_entry_set_nlink(entry, params->nlink);
    }

    /* write the entry header */
    if (archive_write_header(payload, entry) != ARCHIVE_OK) {
        warnx("archive_write_header: %s", archive_error_string(payload));
        archive_entry_free(entry);
        free(full_path);
        free(file_path);
        return -1;
    }

    /* write the data to the payload for regular files but not hardlinks */
    if (S_ISREG(params->mode) && params->size > 0 && params->hardlink == NULL) {
        if ((fd = open(file_path, O_RDONLY)) == -1) {
            warn("open: %s", file_path);
            archive_entry_free(entry);
            free(full_path);
            free(file_path);
            return -1;
        }

        while ((len = read(fd, buf, sizeof(buf))) > 0) {
            archive_write_data(payload, buf, len);
        }

        if (len == -1) {
            warn("read: %s", file_path);
        }

        close(fd);
    }

    /* finalize the entry */
    if (archive_write_finish_entry(payload) != ARCHIVE_OK) {
        warnx("archive_write_finish_entry: %s", archive_error_string(payload));
    }

    /* clean up */
    archive_entry_free(entry);
    free(full_path);
    free(file_path);
    free(relative_path);
    return 0;
}

/*
 * Compress a cpio archive with zstd and no checksum.  librpm has its
 * own cpio code and calls zstd itself, and it does not turn on XXH64
 * checksums.  We use libarchive, which always does.  So we compress
 * here the way librpm does to make sure librpm can read what we
 * wrote.
 *
 * Reads from uncompressed_fd and writes to compressed_fd.  Returns 0
 * on success, -1 on failure.
 */
static int
compress_with_zstd_no_checksum(int uncompressed_fd, int compressed_fd, int level)
{
    size_t zr = 0;
    ZSTD_CCtx *cctx = NULL;
    FILE *in = NULL;
    FILE *out = NULL;
    size_t const isize = ZSTD_CStreamInSize();
    size_t const osize = ZSTD_CStreamOutSize();
    void *ibuf = NULL;
    void *obuf = NULL;
    ZSTD_inBuffer zin;
    ZSTD_outBuffer zout;
    size_t remaining = 0;
    size_t n = 0;
    bool finished = false;
    int rc = 0;

    /* create compression context */
    cctx = ZSTD_createCCtx();

    if (cctx == NULL) {
        warnx("ZSTD_createCCtx failed");
        return -1;
    }

    /* set compression level - must be done before opening the stream */
    zr = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);

    if (ZSTD_isError(zr)) {
        warnx("ZSTD_CCtx_setParameter: %s", ZSTD_getErrorName(zr));
        rc = -1;
        goto cleanup;
    }

    /* disable checksum for rpm compatibility */
    zr = ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);

    if (ZSTD_isError(zr)) {
        warnx("ZSTD_CCtx_setParameter: %s", ZSTD_getErrorName(zr));
        rc = -1;
        goto cleanup;
    }

    /* allocate buffers */
    ibuf = xalloc(isize);
    obuf = xalloc(osize);

    if (ibuf == NULL || obuf == NULL) {
        warn("xalloc");
        rc = -1;
        goto cleanup;
    }

    /* open file descriptors */
    in = fdopen(dup(uncompressed_fd), "rb");
    out = fdopen(dup(compressed_fd), "wb");

    if (in == NULL || out == NULL) {
        warn("fdopen");
        rc = -1;
        goto cleanup;
    }

    /* compress the payload */
    while ((n = fread(ibuf, 1, isize, in)) > 0) {
        zin.src = ibuf;
        zin.size = n;
        zin.pos = 0;
        finished = false;

        do {
            zout.dst = obuf;
            zout.size = osize;
            zout.pos = 0;
            remaining = ZSTD_compressStream2(cctx, &zout, &zin, ZSTD_e_continue);

            if (ZSTD_isError(remaining)) {
                warnx("ZSTD_compressStream2: %s", ZSTD_getErrorName(remaining));
                rc = -1;
                goto cleanup;
            }

            fwrite(obuf, 1, zout.pos, out);

            if (zin.pos == zin.size) {
                finished = true;
            }
        } while (!finished);
    }

    /* loop until all data is flushed */
    do {
        zin.src = NULL;
        zin.size = 0;
        zin.pos = 0;
        zout.dst = obuf;
        zout.size = osize;
        zout.pos = 0;
        remaining = ZSTD_compressStream2(cctx, &zout, &zin, ZSTD_e_end);

        if (ZSTD_isError(remaining)) {
            warnx("ZSTD_compressStream2(end): %s", ZSTD_getErrorName(remaining));
            rc = -1;
            goto cleanup;
        }

        if (zout.pos > 0) {
            fwrite(obuf, 1, zout.pos, out);
        }
    } while (remaining != 0);

cleanup:
    if (cctx) {
        ZSTD_freeCCtx(cctx);
    }

    if (in) {
        fclose(in);
    }

    if (out) {
        fclose(out);
    }

    free(ibuf);
    free(obuf);

    return rc;
}

/*
 * A %ghost file is carried in the file list of the header, but
 * rpmbuild never writes it in to the payload.  Returns true if the
 * file at the given index is a ghost.
 */
static bool
is_ghost_file(const struct hdr_file_lists *hfl, const size_t i)
{
    uint32_t flags = 0;

    if (hfl == NULL || hfl->fileflags == NULL) {
        return false;
    }

    flags = (uint32_t) json_object_get_int64(json_object_array_get_idx(hfl->fileflags, i));

    return (flags & RPMFILE_GHOST) ? true : false;
}

/*
 * Give back the payload path of the file at index i in the header
 * file lists.  The caller has to free it.
 */
static char *
file_list_path(const struct hdr_file_lists *hfl, const size_t i)
{
    int dindex = 0;
    const char *dname = NULL;
    const char *bname = NULL;

    if (hfl == NULL) {
        return NULL;
    }

    dindex = json_object_get_int(json_object_array_get_idx(hfl->dirindexes, i));
    dname = json_object_get_string(json_object_array_get_idx(hfl->dirnames, dindex));
    bname = json_object_get_string(json_object_array_get_idx(hfl->basenames, i));

    return payload_path(dname, bname);
}

/*
 * Grab the file list tags we need from the header tags array.  Tags
 * we do not find stay NULL.
 */
static void
collect_file_list_tags(struct json_object *tags, struct hdr_file_lists *hfl)
{
    size_t i = 0;
    int tagnum = 0;
    struct json_object *entry = NULL;
    struct json_object *tagname = NULL;
    struct json_object *value = NULL;

    if (tags == NULL || hfl == NULL) {
        return;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tagname)) {
            tagnum = rpmTagGetValue(json_object_get_string(tagname));

            if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
                if (tagnum == RPMTAG_BASENAMES) {
                    hfl->basenames = value;
                } else if (tagnum == RPMTAG_DIRNAMES) {
                    hfl->dirnames = value;
                } else if (tagnum == RPMTAG_DIRINDEXES) {
                    hfl->dirindexes = value;
                } else if (tagnum == RPMTAG_FILESIZES) {
                    hfl->filesizes = value;
                } else if (tagnum == RPMTAG_FILEMODES) {
                    hfl->filemodes = value;
                } else if (tagnum == RPMTAG_FILEUIDS) {
                    hfl->fileuids = value;
                } else if (tagnum == RPMTAG_FILEGIDS) {
                    hfl->filegids = value;
                } else if (tagnum == RPMTAG_FILERDEVS) {
                    hfl->filerdevs = value;
                } else if (tagnum == RPMTAG_FILEMTIMES) {
                    hfl->filemtimes = value;
                } else if (tagnum == RPMTAG_FILELINKTOS) {
                    hfl->filelinktos = value;
                } else if (tagnum == RPMTAG_FILEINODES) {
                    hfl->fileinodes = value;
                } else if (tagnum == RPMTAG_FILEFLAGS) {
                    hfl->fileflags = value;
                }
            }
        }
    }

    return;
}

/*
 * Find the hardlink groups in the file list.  RPM wants the last file
 * in a group to hold the data and the ones before it to link to it,
 * so we keep the last path we see for each group.  We allocate the
 * arrays here and free_hardlink_groups() frees them.
 */
static void
find_hardlink_groups(const struct hdr_file_lists *hfl, const size_t numfiles, struct hardlink_groups *hl)
{
    size_t i = 0;
    size_t j = 0;
    uint32_t inode = 0;
    uint16_t mode = 0;
    bool found = false;

    if (hfl == NULL || hl == NULL) {
        return;
    }

    hl->paths = xcalloc(numfiles, sizeof(char *));
    hl->inodes = xcalloc(numfiles, sizeof(uint32_t));
    hl->nlinks = xcalloc(numfiles, sizeof(uint32_t));

    for (i = 0; i < numfiles; i++) {
        inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl->fileinodes, i));
        mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl->filemodes, i));

        /* only regular files can be hardlinks */
        if (!S_ISREG(mode)) {
            continue;
        }

        /* ghost files never make it in to the payload */
        if (is_ghost_file(hfl, i)) {
            continue;
        }

        found = false;

        /* have we seen this inode? */
        for (j = 0; j < hl->count; j++) {
            if (hl->inodes[j] == inode) {
                found = true;
                hl->nlinks[j]++;

                /*
                 * This is now the last occurrence of the hardlink, so
                 * update our tracking structure.
                 */
                free(hl->paths[j]);
                hl->paths[j] = file_list_path(hfl, i);
                break;
            }
        }

        /* new inode, so this is the first occurrence */
        if (!found) {
            hl->paths[hl->count] = file_list_path(hfl, i);
            hl->inodes[hl->count] = inode;
            hl->nlinks[hl->count] = 1;
            hl->count++;
        }
    }

    return;
}

/*
 * Free what find_hardlink_groups() allocated.
 */
static void
free_hardlink_groups(struct hardlink_groups *hl)
{
    size_t i = 0;

    if (hl == NULL) {
        return;
    }

    if (hl->paths) {
        for (i = 0; i < hl->count; i++) {
            free(hl->paths[i]);
        }

        free(hl->paths);
    }

    free(hl->inodes);
    free(hl->nlinks);

    return;
}

/*
 * Work out the order we write the payload entries in.  rpmbuild
 * writes the files that are not in a hardlink group first, then each
 * hardlink group in the order the groups show up in the file list.
 * We put the indexes in order, which holds numfiles of them, and give
 * back how many we used.
 */
static size_t
order_payload_files(const struct hdr_file_lists *hfl, const size_t numfiles, const struct hardlink_groups *hl, size_t *order)
{
    size_t i = 0;
    size_t j = 0;
    size_t ordered = 0;
    uint32_t inode = 0;
    uint16_t mode = 0;
    bool found = false;

    if (hfl == NULL || hl == NULL || order == NULL) {
        return 0;
    }

    for (i = 0; i < numfiles; i++) {
        if (is_ghost_file(hfl, i)) {
            continue;
        }

        inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl->fileinodes, i));
        mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl->filemodes, i));
        found = false;

        if (S_ISREG(mode)) {
            for (j = 0; j < hl->count; j++) {
                if (hl->inodes[j] == inode && hl->nlinks[j] > 1) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            order[ordered++] = i;
        }
    }

    for (j = 0; j < hl->count; j++) {
        if (hl->nlinks[j] < 2) {
            continue;
        }

        for (i = 0; i < numfiles; i++) {
            if (is_ghost_file(hfl, i)) {
                continue;
            }

            inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl->fileinodes, i));
            mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl->filemodes, i));

            if (S_ISREG(mode) && inode == hl->inodes[j]) {
                order[ordered++] = i;
            }
        }
    }

    return ordered;
}

/*
 * Helper for create_rpm() that writes the payload to a temporary
 * file.  Returns an open file descriptor to use later when putting
 * the RPM together, or -1 on failure.  Closing it removes the
 * temporary file, and the caller has to do that.
 */
static int
create_payload(struct json_object *header, const char *payload_dir)
{
    int payloadfd = -1;
    int tmp_payloadfd = -1;
    int dirindex = 0;
    bool need_free_tags = false;
    size_t i = 0;
    size_t j = 0;
    size_t n = 0;
    size_t numfiles = 0;
    size_t ordered = 0;
    size_t *order = NULL;
    char *template = NULL;
    char *opts = NULL;
    char *current_path = NULL;
    const char *tag = NULL;
    const char *level = NULL;
    struct hdr_file_lists hfl;
    struct hardlink_groups hl;
    struct zstd_payload zstd;
    struct file_params params;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *dependencies = NULL;
    struct json_object *tags_with_files = NULL;

    if (header == NULL || payload_dir == NULL) {
        return -1;
    }

    /* get the header tags array */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    /* initialize */
    memset(&hfl, '\0', sizeof(hfl));
    memset(&hl, '\0', sizeof(hl));
    memset(&zstd, '\0', sizeof(zstd));
    zstd.level = 3;

    /* Check if there's a files array that needs to be converted to tags */
    if (json_object_object_get_ex(header, RPM_FILES_DESC, &files)) {
        /* Create a mutable copy of the tags array */
        tags_with_files = json_object_new_array();

        for (i = 0; i < json_object_array_length(tags); i++) {
            json_object_array_add(tags_with_files, json_object_get(json_object_array_get_idx(tags, i)));
        }

        /*
         * Add the file list tags to the copy.  The dependencies go in
         * as well because the depends dictionary rebuilt there holds
         * indexes in to them.
         */
        if (!json_object_object_get_ex(header, RPM_DEPENDENCIES_DESC, &dependencies)) {
            dependencies = NULL;
        }

        add_file_list_tags(tags_with_files, files, payload_dir, dependencies);

        /* Use the copy for processing */
        tags = tags_with_files;
        need_free_tags = true;
    }

    /* Get file lists from header */
    collect_file_list_tags(tags, &hfl);

    if (!hfl.basenames || !hfl.dirnames || !hfl.dirindexes) {
        warnx(_("*** missing file list tags in header"));
        return -1;
    }

    if (!hfl.filesizes || !hfl.filemodes || !hfl.filerdevs || !hfl.filemtimes || !hfl.filelinktos || !hfl.fileinodes) {
        warnx(_("*** missing file metadata tags in header"));
        return -1;
    }

    /* how many files in the payload */
    numfiles = json_object_array_length(hfl.basenames);

    /* get the package name and create a temporary payload file */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_NAME));

    if (tag == NULL) {
        warnx("*** unable to get RPMTAG_NAME");
        tag = "rpm";
    }

    xasprintf(&template, "%s-payload.%d", tag, getpid());
    payloadfd = memfd_create(template, MFD_CLOEXEC);

    if (payloadfd == -1) {
        warn("memfd_create");
        return payloadfd;
    }

    free(template);

    /* create a new payload writer */
    payload = archive_write_new();

    /* set the payload format, which is always the same */
    if (archive_write_set_format_cpio_newc(payload) != ARCHIVE_OK) {
        errx(EXIT_FAILURE, "archive_write_set_format_cpio_newc: %s", archive_error_string(payload));
    }

    /* get the compression algorithm type and level */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR));

    /*
     * For zstd we compress by hand with no checksum.  The rpm cpio
     * reader cannot handle zstd frames with XXH64 checksums and
     * libarchive always adds them.  So we write a plain cpio first
     * and compress it ourselves after.
     */
    if (tag != NULL && !strcmp(tag, "zstd")) {
        zstd.used = true;
        archive_write_add_filter_none(payload);

        /*
         * The cpio stream handed to zstd is not block padded, so do
         * not let libarchive pad out the final block.
         */
        if (archive_write_set_bytes_in_last_block(payload, 1) != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "archive_write_set_bytes_in_last_block: %s", archive_error_string(payload));
        }

        /* get the compression level */
        level = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

        if (level != NULL) {
            errno = 0;
            zstd.level = strtol(level, NULL, 10);

            if (errno == EINVAL || errno == ERANGE) {
                warn("strtol");
                zstd.level = 3;
            }

            if (zstd.level < 1) {
                zstd.level = 1;
            } else if (zstd.level > 22) {
                zstd.level = 22;
            }
        }
    } else if (tag == NULL || !strcmp(tag, "none")) {
        /* no compression, but RPM still needs 512-byte blocks for the cpio payload */
        archive_write_add_filter_none(payload);

        if (archive_write_set_bytes_per_block(payload, 512) != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "archive_write_set_bytes_per_block: %s", archive_error_string(payload));
        }

        if (archive_write_set_bytes_in_last_block(payload, 512) != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "archive_write_set_bytes_in_last_block: %s", archive_error_string(payload));
        }
    } else if (!strcmp(tag, "gzip")) {
        archive_write_add_filter_gzip(payload);
    } else if (!strcmp(tag, "bzip2")) {
        archive_write_add_filter_bzip2(payload);
    } else if (!strcmp(tag, "xz")) {
        archive_write_add_filter_xz(payload);
    } else if (!strcmp(tag, "lzma")) {
        archive_write_add_filter_lzma(payload);
    } else {
        /* unknown compression - default to none with 512 byte blocks */
        archive_write_add_filter_none(payload);

        if (archive_write_set_bytes_per_block(payload, 512) != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "archive_write_set_bytes_per_block: %s", archive_error_string(payload));
        }

        if (archive_write_set_bytes_in_last_block(payload, 512) != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "archive_write_set_bytes_in_last_block: %s", archive_error_string(payload));
        }
    }

    /* set the compression level for non-zstd compressors */
    if (!zstd.used) {
        tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

        if (tag != NULL) {
            xasprintf(&opts, "compression-level=%s", tag);
            archive_write_set_options(payload, opts);
            free(opts);
        }
    }

    /* open the payload for writing - use pipe for zstd to avoid single-segment mode */
    if (zstd.used) {
        if (pipe(zstd.pipefd) == -1) {
            warn("pipe");
            return -1;
        }

        tmp_payloadfd = zstd.pipefd[1];

        /* fork a process to compress from pipe to final fd */
        zstd.pid = fork();

        if (zstd.pid == -1) {
            warn("fork");

            if (close(zstd.pipefd[0]) == -1) {
                warn("close");
            }

            if (close(zstd.pipefd[1]) == -1) {
                warn("close");
            }

            return -1;
        } else if (zstd.pid == 0) {
            /* read from pipe, compress, write to payloadfd */
            if (close(zstd.pipefd[1]) == -1) {
                warn("close");
            }

            if (compress_with_zstd_no_checksum(zstd.pipefd[0], payloadfd, zstd.level) != 0) {
                _exit(1);
            }

            if (close(zstd.pipefd[0]) == -1) {
                warn("close");
            }

            _exit(0);
        }

        if (close(zstd.pipefd[0]) == -1) {
            warn("close");
        }
    } else {
        tmp_payloadfd = payloadfd;
    }

    if (archive_write_open_fd(payload, tmp_payloadfd) != ARCHIVE_OK) {
        errx(EXIT_FAILURE, "archive_write_open_fd: %s", archive_error_string(payload));
    }

    /* we have to track hardlinks by inode number and link count */
    find_hardlink_groups(&hfl, numfiles, &hl);

    /* we do not write the payload entries in file list order */
    order = xcalloc(numfiles, sizeof(size_t));
    ordered = order_payload_files(&hfl, numfiles, &hl, order);

    /* Write each file from the header to the payload */
    for (n = 0; n < ordered; n++) {
        i = order[n];
        dirindex = json_object_get_int(json_object_array_get_idx(hfl.dirindexes, i));
        params.dirname = json_object_get_string(json_object_array_get_idx(hfl.dirnames, dirindex));
        params.basename = json_object_get_string(json_object_array_get_idx(hfl.basenames, i));
        params.size = json_object_get_int64(json_object_array_get_idx(hfl.filesizes, i));
        params.mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl.filemodes, i));
        params.rdev = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl.filerdevs, i));
        params.mtime = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.filemtimes, i));
        params.linkto = json_object_get_string(json_object_array_get_idx(hfl.filelinktos, i));

        /* we may not have fileuids or filegids, so default to 0 and then try */
        params.uid = 0;
        params.gid = 0;

        if (hfl.fileuids) {
            params.uid = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.fileuids, i));
        }

        if (hfl.filegids) {
            params.gid = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.filegids, i));
        }

        /*
         * every payload entry carries the inode number from the
         * header, but only regular files can be hardlinks
         */
        params.inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.fileinodes, i));
        params.nlink = 1;
        params.hardlink = NULL;

        if (S_ISREG(params.mode)) {
            /* find the first occurrence of this inode */
            for (j = 0; j < hl.count; j++) {
                if (hl.inodes[j] == params.inode) {
                    params.nlink = hl.nlinks[j];

                    /* build current file path */
                    current_path = payload_path(params.dirname, params.basename);

                    /* set hardlink target */
                    if (strcmp(current_path, hl.paths[j]) != 0) {
                        params.hardlink = hl.paths[j];
                    }

                    free(current_path);
                    break;
                }
            }
        }

        if (add_file_to_payload(payload_dir, &params) != 0) {
            warnx("failed to add file: %s%s", params.dirname, params.basename);
        }
    }

    /* close the payload so data can be flushed */
    if (archive_write_close(payload) != ARCHIVE_OK) {
        warnx("archive_write_close: %s", archive_error_string(payload));
    }

#if ARCHIVE_VERSION_NUMBER < 3000000
    archive_write_finish(payload);
#else
    if (archive_write_free(payload) != ARCHIVE_OK) {
        warnx("archive_write_free: %s", archive_error_string(payload));
    }
#endif

    /* clean up hardlink tracking */
    free_hardlink_groups(&hl);
    free(order);

    /* when using zstd, close pipe and wait for compression child */
    if (zstd.used) {
        /* closing the pipe tells child EOF */
        if (close(tmp_payloadfd) == -1) {
            warn("close");
        }

        if (waitpid(zstd.pid, &zstd.status, 0) == -1) {
            warn("waitpid");
        }

        if (!WIFEXITED(zstd.status) || WEXITSTATUS(zstd.status) != 0) {
            errx(EXIT_FAILURE, "zstd compression child failed");
        }
    }

    /* cleanup temporary tags array if we created one */
    if (need_free_tags) {
        json_object_put(tags);
    }

    /* back to the beginning */
    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
    }

    return payloadfd;
}

/*
 * Called by create_rpm() to copy the payload from the given file
 * descriptor to the current spot in the RPM file.
 *
 * NOTE:
 * This closes the file descriptor, which removes the temporary
 * payload file.
 */
static int
write_payload(FILE *rpm, int fd)
{
    int r = 0;
    FILE *pload = NULL;
    char buf[BUFSIZ];
    size_t s = 0;

    if (rpm == NULL) {
        return 0;
    }

    /* bring the payload back to the beginning */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        warn("lseek");
        return -1;
    }

    /* open the payload for reading */
    pload = fdopen(fd, "rb");

    if (pload == NULL) {
        warn("fdopen");
        return -1;
    }

    /* copy payload over to the RPM */
    while ((s = fread(buf, sizeof(char), BUFSIZ, pload)) > 0) {
        if (fwrite(buf, sizeof(char), s, rpm) != s) {
            warn("fwrite");
            r = -1;
            break;
        }
    }

    /* close the payload -- deletes the temporary file */
    if (fclose(pload) != 0) {
        warn("fclose");
        return -1;
    }

    return r;
}

/*
 * Compute the SHA-256 digest of the uncompressed payload, which is
 * what RPM records in RPMTAG_PAYLOADSHA256ALT.  We already wrote the
 * compressed payload in to payloadfd, so we read it back through the
 * libarchive filters to get the cpio stream again.  Returns the
 * digest as an allocated hex string or NULL on failure.  Caller must
 * free it.  If size is not NULL, the uncompressed byte count goes
 * there.
 */
static char *
uncompressed_payload_digest(const int payloadfd, uint64_t *size)
{
    unsigned int i = 0;
    unsigned int digestlen = 0;
    ssize_t len = 0;
    uint64_t total = 0;
    char *r = NULL;
    struct archive *raw = NULL;
    struct archive_entry *entry = NULL;
    EVP_MD_CTX *ctx = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];
    char buf[BUFSIZ];

    if (payloadfd == -1) {
        return NULL;
    }

    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
        return NULL;
    }

    raw = archive_read_new();
    archive_read_support_filter_all(raw);
    archive_read_support_format_raw(raw);

    if (archive_read_open_fd(raw, payloadfd, BUFSIZ) != ARCHIVE_OK) {
        warnx("archive_read_open_fd: %s", archive_error_string(raw));
        archive_read_free(raw);
        return NULL;
    }

    ctx = EVP_MD_CTX_new();

    if (ctx == NULL || EVP_DigestInit(ctx, EVP_sha256()) == 0) {
        warn("EVP_DigestInit");
        goto cleanup_uncompressed_payload_digest;
    }

    if (archive_read_next_header(raw, &entry) != ARCHIVE_OK) {
        warnx("archive_read_next_header: %s", archive_error_string(raw));
        goto cleanup_uncompressed_payload_digest;
    }

    while ((len = archive_read_data(raw, buf, sizeof(buf))) > 0) {
        if (EVP_DigestUpdate(ctx, buf, len) == 0) {
            warn("EVP_DigestUpdate");
            goto cleanup_uncompressed_payload_digest;
        }

        total += len;
    }

    if (len < 0) {
        warnx("archive_read_data: %s", archive_error_string(raw));
        goto cleanup_uncompressed_payload_digest;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digestlen) == 0) {
        warn("EVP_DigestFinal_ex");
        goto cleanup_uncompressed_payload_digest;
    }

    r = xcalloc((digestlen * 2) + 1, sizeof(char));

    for (i = 0; i < digestlen; ++i) {
        sprintf(&r[i * 2], "%02x", (unsigned int) digest[i]);
    }

    if (size != NULL) {
        *size = total;
    }

cleanup_uncompressed_payload_digest:
    EVP_MD_CTX_free(ctx);
    archive_read_free(raw);

    return r;
}

/*
 * Replace the first element of the string array value of the named
 * header tag with the given digest string.  Tags that the package does
 * not carry are left alone.  Returns non-zero on failure.
 */
static int
set_payload_digest(struct json_object *tags, const rpmTagVal tagnum, const char *digest)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *tag = NULL;
    struct json_object *value = NULL;

    if (tags == NULL || digest == NULL) {
        return -1;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (!json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tag)) {
            continue;
        }

        if (rpmTagGetValue(json_object_get_string(tag)) != tagnum) {
            continue;
        }

        if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
            return -1;
        }

        if (json_object_get_type(value) != json_type_array || json_object_array_length(value) < 1) {
            return -1;
        }

        json_object_array_put_idx(value, 0, json_object_new_string(digest));
        break;
    }

    return 0;
}

/*
 * Update the payload digests in the header.  RPMTAG_PAYLOADSHA256 is
 * the digest of the payload as it is written in to the RPM, which is
 * the compressed payload, and RPMTAG_PAYLOADSHA256ALT is the digest of
 * the uncompressed payload.  Returns non-zero on failure.
 */
static int
update_header_digests(struct json_object *header, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const int payloadfd)
{
    int i = 0;
    char *buf = NULL;
    unsigned char *digest = NULL;
    struct json_object *tags = NULL;

    if (header == NULL || hdr == NULL || hdrinfo == NULL || payloadfd == -1) {
        return -1;
    }

    /* get the tags array from the header */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    /* compute the SHA-256 digest of the compressed payload */
    digest = mksigdigest(TARPM_DIGEST_SHA256_PAYLOAD, hdr, hdrinfo, header, payloadfd);

    if (digest == NULL) {
        warnx(_("*** failed to compute the payload SHA-256 digest"));
        return -1;
    }

    buf = xcalloc((SHA256_DIGEST_LENGTH * 2) + 1, sizeof(char));

    for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        sprintf(&buf[i * 2], "%02x", (unsigned int) digest[i]);
    }

    free(digest);

    if (set_payload_digest(tags, 5092, buf) != 0) {
        warnx(_("*** failed to update the payload SHA-256 digest"));
        free(buf);
        return -1;
    }

    free(buf);

#ifdef _USE_RPMTAG_5097
    /* compute the SHA-256 digest of the uncompressed payload */
    buf = uncompressed_payload_digest(payloadfd, NULL);

    if (buf == NULL) {
        warnx(_("*** failed to compute the payload SHA-256 ALT digest"));
        return -1;
    }

    if (set_payload_digest(tags, 5097, buf) != 0) {
        warnx(_("*** failed to update the payload SHA-256 ALT digest"));
        free(buf);
        return -1;
    }

    free(buf);
#endif

    return 0;
}

/*
 * Update the digests and sizes in the signature using the header and
 * payload data.  Returns non-zero on failure.
 */
static int
update_signature(struct json_object *signature, struct json_object *header, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const int payloadfd)
{
    int i = 0;
    unsigned char *digest = NULL;
    void *blob = NULL;
    char *buf = NULL;
    struct json_object *tags = NULL;
    off_t payload_off = 0;
    uint64_t payloadsize = 0;
    uint64_t archivesize = 0;
    uint64_t totalsize = 0;
    uint64_t hdrsize = 0;
    uint64_t nentries = 0;
    uint64_t nbytes = 0;

    if (signature == NULL || header == NULL || hdr == NULL || hdrinfo == NULL || payloadfd == -1) {
        return -1;
    }

    /* get the tags array from the signature */
    if (json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in signature data"));
        return -1;
    }

    /* get the compressed payload size */
    payload_off = lseek(payloadfd, 0, SEEK_END);

    if (payload_off == -1) {
        warn("lseek");
        return -1;
    }

    payloadsize = payload_off;

    /* reset file position back to beginning */
    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
        return -1;
    }

    /* calculate the header size */
    nentries = ntohl(hdr->nentries);
    nbytes = ntohl(hdr->nbytes);
    hdrsize = sizeof(*hdr) + (nentries * sizeof(struct rpmhdrentry)) + nbytes;
    totalsize = hdrsize + payloadsize;

    /* update Size tag (header + payload) */
    xasprintf(&buf, "%lu", totalsize);

    if (payloadsize > UINT32_MAX) {
        if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_LONGSIZE), buf) != 0) {
            warnx(_("*** failed to update Longsize in signature"));
            free(buf);
            return -1;
        }
    } else {
        if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_SIZE), buf) != 0) {
            warnx(_("*** failed to update Size in signature"));
            free(buf);
            return -1;
        }
    }

    free(buf);

    /*
     * update the Payloadsize tag.  This is the size of the
     * uncompressed payload, not of what we write in to the RPM
     */
    buf = uncompressed_payload_digest(payloadfd, &archivesize);

    if (buf == NULL) {
        warnx(_("*** failed to measure the uncompressed payload"));
        return -1;
    }

    free(buf);
    xasprintf(&buf, "%lu", archivesize);

    if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_PAYLOADSIZE), buf) != 0) {
        warnx(_("*** failed to update Payloadsize in signature"));
        free(buf);
        return -1;
    }

    free(buf);

    /* compute MD5 digest */
    digest = mksigdigest(TARPM_DIGEST_MD5, hdr, hdrinfo, header, payloadfd);

    if (digest == NULL) {
        warnx(_("*** failed to compute the signature MD5 digest"));
        return -1;
    }

    blob = xalloc(MD5_DIGEST_LENGTH);
    memcpy(blob, digest, MD5_DIGEST_LENGTH);
    buf = rpmBase64Encode(blob, MD5_DIGEST_LENGTH, -1);
    free(blob);

    if (buf == NULL) {
        warnx("rpmBase64Encode");
        free(digest);
        return -1;
    }

    if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_MD5), buf) != 0) {
        warnx(_("*** failed to update MD5 digest in signature"));
        free(digest);
        free(buf);
        return -1;
    }

    free(digest);
    free(buf);

    /* compute SHA-1 digest */
    digest = mksigdigest(TARPM_DIGEST_SHA1, hdr, hdrinfo, header, payloadfd);

    if (digest == NULL) {
        warnx(_("*** failed to compute the signature SHA-1 digest"));
        return -1;
    }

    buf = xcalloc(SHA_DIGEST_LENGTH * 2 + 1, sizeof(char));

    for (i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        sprintf(&buf[i * 2], "%02x", (unsigned int) digest[i]);
    }

    if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_SHA1), buf) != 0) {
        warnx(_("*** failed to update SHA-1 digest in signature"));
        free(digest);
        free(buf);
        return -1;
    }

    free(digest);
    free(buf);

    /* compute SHA-256 digest */
    digest = mksigdigest(TARPM_DIGEST_SHA256, hdr, hdrinfo, header, payloadfd);

    if (digest == NULL) {
        warnx(_("*** failed to compute the signature SHA-256 digest"));
        return -1;
    }

    buf = xcalloc(SHA256_DIGEST_LENGTH * 2 + 1, sizeof(char));

    for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        sprintf(&buf[i * 2], "%02x", (unsigned int) digest[i]);
    }

    if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_SHA256), buf) != 0) {
        warnx(_("*** failed to update SHA-256 digest in signature"));
        free(digest);
        free(buf);
        return -1;
    }

    free(digest);
    free(buf);

    return 0;
}

/* Handler for -c mode (create) */
int
create_rpm(const char *filename, const char *cwd, const char *input_dir, const struct json_paths *paths)
{
    FILE *rpm = NULL;
    int payloadfd = -1;
    struct stat sb;
    char *header_dir = NULL;
    char *payload_dir = NULL;
    const char *signature_path = OUTPUT_SIGNATURE;
    const char *header_path = OUTPUT_HEADER;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct rpmlead *rawlead = NULL;
    struct rpmhdr *sig = NULL;
    struct rpmhdrinfo *siginfo = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;

    if (filename == NULL || cwd == NULL || input_dir == NULL) {
        return -1;
    }

    /* make sure the input directory exists */
    if (access(input_dir, R_OK|X_OK)) {
        warnx(_("*** %s does not exist"), input_dir);
        return -1;
    }

    /* change to the input directory */
    if (chdir(input_dir) == -1) {
        warn("chdir");
        return -1;
    }

    /* the caller may have named where the payload tree is */
    if (paths != NULL && paths->payload != NULL) {
        payload_dir = strdup(paths->payload);
    } else {
        payload_dir = joinpath(input_dir, PAYLOAD_SUBDIR, NULL);
    }

    if (payload_dir == NULL) {
        warnx(_("*** unable to set payload_dir"));
        return -1;
    }

    /* make sure we have the payload subdirectory */
    if (lstat(payload_dir, &sb) == -1) {
        warn("lstat");
        free(payload_dir);
        return -1;
    }

    if (!S_ISDIR(sb.st_mode)) {
        warn(_("*** %s is not a directory"), payload_dir);
        free(payload_dir);
        return -1;
    }

    /* the caller may have named where the JSON metadata files are */
    if (paths != NULL && paths->signature != NULL) {
        signature_path = paths->signature;
    }

    if (paths != NULL && paths->header != NULL) {
        header_path = paths->header;

        /* tag values kept in their own file sit next to header.json */
        header_dir = dir_name(header_path);
    } else {
        header_dir = strdup(input_dir);
    }

    if (header_dir == NULL) {
        warnx(_("*** unable to set header_dir"));
        free(payload_dir);
        return -1;
    }

    /* read in signature.json and header.json */
    signature = read_json_file(signature_path);

    if (signature == NULL) {
        warnx(_("*** missing signature data"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    header = read_json_file(header_path);

    if (header == NULL) {
        warnx(_("*** missing header data"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* pick up the files added to the payload tree */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) && json_object_object_get_ex(header, RPM_FILES_DESC, &files)) {
        add_payload_files(tags, files, payload_dir);
    }

    /* create the lead from header metadata */
    rawlead = create_lead(header);

    if (rawlead == NULL) {
        warnx(_("*** unable to construct RPM lead"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* create the header (the main header) */
    if (create_header(header, &hdr, &hdrinfo, payload_dir, header_dir, false) == -1) {
        warnx(_("*** unable to construct RPM header"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* create the payload */
    payloadfd = create_payload(header, payload_dir);

    if (payloadfd == -1) {
        warnx("create_payload");
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* create the signature */
    if (create_header(signature, &sig, &siginfo, NULL, NULL, true) == -1) {
        warnx(_("*** unable to construct RPM signature"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* update the header payload digest (payload only) */
    if (update_header_digests(header, hdr, hdrinfo, payloadfd) != 0) {
        close(payloadfd);
        warnx("update_header_digests");
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* free the old header structures before regenerating */
    free_header(hdr, hdrinfo);

    /* regenerate the header with updated digests */
    if (create_header(header, &hdr, &hdrinfo, payload_dir, header_dir, false) == -1) {
        warnx(_("*** unable to reconstruct RPM header"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* recalculate the digests and update the signature data using the updated header */
    if (update_signature(signature, header, hdr, hdrinfo, payloadfd) != 0) {
        close(payloadfd);
        warnx("update_signature");
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* free the old signature structures before regenerating */
    free_header(sig, siginfo);

    /* regenerate the signature with updated digests */
    if (create_header(signature, &sig, &siginfo, NULL, NULL, true) == -1) {
        warnx(_("*** unable to reconstruct RPM signature"));
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* create an RPM for writing */
    rpm = fopen(filename, "wb");

    if (rpm == NULL) {
        warn("fopen");
        free(header_dir);
        free(payload_dir);
        return -1;
    }

    /* write the lead to the RPM */
    if (fwrite(rawlead, sizeof(*rawlead), 1, rpm) != 1) {
        warn("fwrite");
    }

    /* write the signature to the RPM */
    if (write_header(rpm, sig, siginfo, true, signature) == -1) {
        goto create_cleanup;
    }

    /* write the header to the RPM */
    if (write_header(rpm, hdr, hdrinfo, false, header) == -1) {
        goto create_cleanup;
    }

    /* write the payload to the RPM */
    if (write_payload(rpm, payloadfd) == -1) {
        goto create_cleanup;
    }

create_cleanup:
    json_object_put(header);
    json_object_put(signature);
    free_header(sig, siginfo);
    free_header(hdr, hdrinfo);
    free(rawlead);
    free(header_dir);
    free(payload_dir);

    /* close the RPM */
    if (fclose(rpm) != 0) {
        warn("fclose");
    }

    /* back to the starting point and clean up */
    if (chdir(cwd) == -1) {
        warn("chdir");
    }

    return 0;
}
