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
#include <archive.h>
#include <archive_entry.h>
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
 * Helper function used to build the RPM payload.  We use libarchive,
 * but the payload has to follow the metadata in the header rather
 * than walking the filesystem tree.
 */
static int
add_file_to_payload(const char *input_dir, const struct file_params *params)
{
    char *relative_path = NULL;
    char *full_path = NULL;
    char *file_path = NULL;
    struct archive_entry *entry = NULL;
    int fd = -1;
    ssize_t len = 0;
    char buf[BUFSIZ];

    if (input_dir == NULL || params == NULL) {
        return -1;
    }

    /* the relative path is where this file will live when installed */
    relative_path = joinpath(params->dirname, params->basename, NULL);

    /* this is the actual location of the file going in the payload */
    file_path = joinpath(input_dir, params->payload_subdir, relative_path, NULL);

    /* start a new entry */
    entry = archive_entry_new();

    /*
     * the path in the cpio payload needs to begin with "./"
     */
    xasprintf(&full_path, ".%s", relative_path);
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
 * Helper function to compress a cpio archive with zstd without
 * checksum.  librpm has its own internal cpio code and uses zstd
 * directly.  tarpm uses libarchive and it handles creating the cpio
 * payload and zstd compression.  But librpm does not enable XXH64
 * checksums, but libarchive does.  To ensure librpm can read a zstd
 * compressed payload created by tarpm, we need our own zstd
 * compression function to do it the librpm way.
 *
 * Reads from uncompressed_fd and writes compressed data to compressed_fd.
 * Returns 0 on success, -1 on failure.
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
 * Helper for create_rpm() that writes the payload data to a temporary
 * file.  Returns an open file descriptor that can then be used later
 * when putting together the final RPM.  Caller must close the file
 * descriptor when done which will remove the temporary file
 * associated with it.
 *
 * Returns -1 on failure.
 */
static int
create_payload(struct json_object *header, const char *input_dir, const char *payload_subdir)
{
    int payloadfd = -1;
    int tmp_payloadfd = -1;
    char *template = NULL;
    const char *tag = NULL;
    const char *level = NULL;
    char *opts = NULL;
    struct hdr_file_lists hfl;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *tagname = NULL;
    struct json_object *value = NULL;
    int dirindex = 0;
    size_t i = 0;
    size_t j = 0;
    size_t numfiles = 0;
    int tagnum = 0;
    struct file_params params;
    bool use_zstd = false;
    int zstd_level = 3;
    int zstd_pipefd[2];
    pid_t zstd_pid = 0;
    int status = 0;
    char **hardlink_paths = NULL;
    uint32_t *hardlink_inodes = NULL;
    uint32_t *hardlink_nlinks = NULL;
    size_t hardlink_count = 0;
    char *path = NULL;
    char *current_path = NULL;
    uint32_t inode = 0;
    uint16_t mode = 0;
    int dindex = 0;
    const char *dname = NULL;
    const char *bname = NULL;
    bool found = false;

    if (header == NULL || payload_subdir == NULL) {
        return -1;
    }

    /* get the header tags array */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    /* initialize */
    memset(&hfl, '\0', sizeof(hfl));

    /* Get file lists from header */
    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tagname)) {
            tagnum = rpmTagGetValue(json_object_get_string(tagname));

            if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
                if (tagnum == RPMTAG_BASENAMES) {
                    hfl.basenames = value;
                } else if (tagnum == RPMTAG_DIRNAMES) {
                    hfl.dirnames = value;
                } else if (tagnum == RPMTAG_DIRINDEXES) {
                    hfl.dirindexes = value;
                } else if (tagnum == RPMTAG_FILESIZES) {
                    hfl.filesizes = value;
                } else if (tagnum == RPMTAG_FILEMODES) {
                    hfl.filemodes = value;
                } else if (tagnum == RPMTAG_FILEUIDS) {
                    hfl.fileuids = value;
                } else if (tagnum == RPMTAG_FILEGIDS) {
                    hfl.filegids = value;
                } else if (tagnum == RPMTAG_FILERDEVS) {
                    hfl.filerdevs = value;
                } else if (tagnum == RPMTAG_FILEMTIMES) {
                    hfl.filemtimes = value;
                } else if (tagnum == RPMTAG_FILELINKTOS) {
                    hfl.filelinktos = value;
                } else if (tagnum == RPMTAG_FILEINODES) {
                    hfl.fileinodes = value;
                }
            }
        }
    }

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
     * For zstd, we need to compress manually without checksum for rpm compatibility.
     * RPM's cpio reader cannot handle zstd frames with XXH64 checksums, but
     * libarchive always enables them. Create uncompressed cpio first, then
     * compress it manually with checksum disabled.
     */
    if (tag != NULL && !strcmp(tag, "zstd")) {
        use_zstd = true;
        archive_write_add_filter_none(payload);

        /* get the compression level */
        level = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

        if (level != NULL) {
            errno = 0;
            zstd_level = strtol(level, NULL, 10);

            if (errno == EINVAL || errno == ERANGE) {
                warn("strtol");
                zstd_level = 3;
            }

            if (zstd_level < 1) {
                zstd_level = 1;
            } else if (zstd_level > 22) {
                zstd_level = 22;
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
    if (!use_zstd) {
        tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

        if (tag != NULL) {
            xasprintf(&opts, "compression-level=%s", tag);
            archive_write_set_options(payload, opts);
            free(opts);
        }
    }

    /* open the payload for writing - use pipe for zstd to avoid single-segment mode */
    if (use_zstd) {
        if (pipe(zstd_pipefd) == -1) {
            warn("pipe");
            return -1;
        }

        tmp_payloadfd = zstd_pipefd[1];

        /* fork a process to compress from pipe to final fd */
        zstd_pid = fork();

        if (zstd_pid == -1) {
            warn("fork");

            if (close(zstd_pipefd[0]) == -1) {
                warn("close");
            }

            if (close(zstd_pipefd[1]) == -1) {
                warn("close");
            }

            return -1;
        } else if (zstd_pid == 0) {
            /* read from pipe, compress, write to payloadfd */
            if (close(zstd_pipefd[1]) == -1) {
                warn("close");
            }

            if (compress_with_zstd_no_checksum(zstd_pipefd[0], payloadfd, zstd_level) != 0) {
                _exit(1);
            }

            if (close(zstd_pipefd[0]) == -1) {
                warn("close");
            }

            _exit(0);
        }

        if (close(zstd_pipefd[0]) == -1) {
            warn("close");
        }
    } else {
        tmp_payloadfd = payloadfd;
    }

    if (archive_write_open_fd(payload, tmp_payloadfd) != ARCHIVE_OK) {
        errx(EXIT_FAILURE, "archive_write_open_fd: %s", archive_error_string(payload));
    }

    /*
     * Hardlink tracking structure.  We have to track hardlinks by
     * inode number and link count.
     */
    hardlink_paths = xcalloc(numfiles, sizeof(char *));
    hardlink_inodes = xcalloc(numfiles, sizeof(uint32_t));
    hardlink_nlinks = xcalloc(numfiles, sizeof(uint32_t));

    /*
     * RPM expects the last file in each hardlink group to have the
     * data, with earlier files as hardlinks to it.  Find the last
     * occurrence of each inode.
     */
    for (i = 0; i < numfiles; i++) {
        inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.fileinodes, i));
        mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl.filemodes, i));

        /* only regular files can be hardlinks */
        if (!S_ISREG(mode)) {
            continue;
        }

        found = false;

        /* have we seen this inode? */
        for (j = 0; j < hardlink_count; j++) {
            if (hardlink_inodes[j] == inode) {
                found = true;
                hardlink_nlinks[j]++;

                /*
                 * This is now the last occurrence of the hardlink, so
                 * update our tracking structure.
                 */
                dindex = json_object_get_int(json_object_array_get_idx(hfl.dirindexes, i));
                dname = json_object_get_string(json_object_array_get_idx(hfl.dirnames, dindex));
                bname = json_object_get_string(json_object_array_get_idx(hfl.basenames, i));

                if (dname[0] == '/') {
                    xasprintf(&path, ".%s%s", dname, bname);
                } else {
                    xasprintf(&path, "./%s%s", dname, bname);
                }

                /* replace the old path */
                free(hardlink_paths[j]);
                hardlink_paths[j] = path;
                break;
            }
        }

        /* new inode, so this is the first occurrence */
        if (!found) {
            dindex = json_object_get_int(json_object_array_get_idx(hfl.dirindexes, i));
            dname = json_object_get_string(json_object_array_get_idx(hfl.dirnames, dindex));
            bname = json_object_get_string(json_object_array_get_idx(hfl.basenames, i));

            if (dname[0] == '/') {
                xasprintf(&path, ".%s%s", dname, bname);
            } else {
                xasprintf(&path, "./%s%s", dname, bname);
            }

            hardlink_paths[hardlink_count] = path;
            hardlink_inodes[hardlink_count] = inode;
            hardlink_nlinks[hardlink_count] = 1;
            hardlink_count++;
        }
    }

    /* Write each file from the header to the payload */
    for (i = 0; i < numfiles; i++) {
        dirindex = json_object_get_int(json_object_array_get_idx(hfl.dirindexes, i));
        params.dirname = json_object_get_string(json_object_array_get_idx(hfl.dirnames, dirindex));
        params.basename = json_object_get_string(json_object_array_get_idx(hfl.basenames, i));
        params.payload_subdir = payload_subdir;
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

        /* check if this is a hardlink */
        params.inode = 0;
        params.nlink = 0;
        params.hardlink = NULL;

        if (S_ISREG(params.mode)) {
            params.inode = (uint32_t) json_object_get_int(json_object_array_get_idx(hfl.fileinodes, i));

            /* find the first occurrence of this inode */
            for (j = 0; j < hardlink_count; j++) {
                if (hardlink_inodes[j] == params.inode) {
                    params.nlink = hardlink_nlinks[j];

                    /* build current file path */
                    if (params.dirname[0] == '/') {
                        xasprintf(&current_path, ".%s%s", params.dirname, params.basename);
                    } else {
                        xasprintf(&current_path, "./%s%s", params.dirname, params.basename);
                    }

                    /* set hardlink target */
                    if (strcmp(current_path, hardlink_paths[j]) != 0) {
                        params.hardlink = hardlink_paths[j];
                    }

                    free(current_path);
                    break;
                }
            }
        }

        if (add_file_to_payload(input_dir, &params) != 0) {
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
    if (hardlink_paths) {
        for (i = 0; i < hardlink_count; i++) {
            free(hardlink_paths[i]);
        }

        free(hardlink_paths);
    }

    free(hardlink_inodes);
    free(hardlink_nlinks);

    /* when using zstd, close pipe and wait for compression child */
    if (use_zstd) {
        /* closing the pipe tells child EOF */
        if (close(tmp_payloadfd) == -1) {
            warn("close");
        }

        if (waitpid(zstd_pid, &status, 0) == -1) {
            warn("waitpid");
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            errx(EXIT_FAILURE, "zstd compression child failed");
        }
    }

    /* back to the beginning */
    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
    }

    return payloadfd;
}

/*
 * Called by create_rpm() to copy the payload from the specified file
 * descriptor to the current position in the RPM file.
 *
 * NOTE:
 * This does close the file descriptor for the temporary payload which
 * does remove that temporary file.
 */
static int
write_payload(FILE *rpm, int fd)
{
    int r = 0;
    FILE *pload = NULL;
    char buf[BUFSIZ];
    size_t s = 0;
    size_t total = 0;

    if (rpm == NULL) {
        return 0;
    }

    /* bring the payload back to the beginning */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        warn("lseek");
        r = -1;
    }

    /* open the payload for reading */
    pload = fdopen(fd, "rb");

    if (pload == NULL) {
        warn("fdopen");
        r = -1;
    }

    /* copy payload over to the RPM */
    while ((s = fread(buf, sizeof(char), BUFSIZ, pload)) > 0) {
        total += s;

        if (fwrite(buf, sizeof(char), s, rpm) != s) {
            warn("fwrite");
            r = -1;
            break;
        }
    }

    /* close the payload -- deletes the temporary file */
    if (fclose(pload) != 0) {
        warn("fclose");
        r = -1;
    }

    return r;
}

/*
 * Update the payload digest in the header. Computes payload-only digest
 * that is stored in the signature header.  Returns non-zero on failure.
 */
static int
update_header_digests(struct json_object *header, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const int payloadfd)
{
    int i = 0;
    unsigned char *digest = NULL;
    char *buf = NULL;
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *tag = NULL;
    struct json_object *value = NULL;
    size_t j = 0;

    if (header == NULL || hdr == NULL || hdrinfo == NULL || payloadfd == -1) {
        return -1;
    }

    /* get the tags array from the header */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    /* compute SHA-256 PAYLOAD digest (payload only) */
    digest = mksigdigest(TARPM_DIGEST_SHA256_PAYLOAD, hdr, hdrinfo, header, payloadfd);

    if (digest == NULL) {
        warnx(_("*** failed to compute SHA-256 ALT digest"));
        return -1;
    }

    buf = xcalloc(SHA256_DIGEST_LENGTH * 2 + 1, sizeof(char));

    for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        sprintf(&buf[i * 2], "%02x", (unsigned int) digest[i]);
    }

    /* find and update the RPMTAG_PAYLOADSHA256ALT */
    for (j = 0; j < json_object_array_length(tags); j++) {
        entry = json_object_array_get_idx(tags, j);

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &tag)) {
            if (rpmTagGetValue(json_object_get_string(tag)) == RPMTAG_PAYLOADSHA256ALT) {
                if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
                    /* Update the first element of the array */
                    if (json_object_get_type(value) == json_type_array && json_object_array_length(value) > 0) {
                        json_object_array_put_idx(value, 0, json_object_new_string(buf));
                    }
                }

                break;
            }
        }
    }

    free(digest);
    free(buf);

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

    /* update Payloadsize tag (compressed payload size) */
    xasprintf(&buf, "%lu", payloadsize);

    if (set_tag_value(tags, sig_tag_name(RPMSIGTAG_PAYLOADSIZE), buf) != 0) {
        warnx(_("*** failed to update Payloadsize in signature"));
        free(buf);
        return -1;
    }

    free(buf);

    /* compute MD5 digest */
    digest = mksigdigest(TARPM_DIGEST_MD5, hdr, hdrinfo, header, payloadfd);
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
void
create_rpm(const char *filename, const char *cwd, const char *input_dir)
{
    FILE *rpm = NULL;
    int payloadfd = -1;
    struct stat sb;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    struct rpmlead *rawlead = NULL;
    struct rpmhdr *sig = NULL;
    struct rpmhdrinfo *siginfo = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;

    if (filename == NULL || cwd == NULL || input_dir == NULL) {
        return;
    }

    /* make sure the input directory exists */
    if (access(input_dir, R_OK|X_OK)) {
        errx(EXIT_FAILURE, _("*** %s does not exist"), input_dir);
    }

    /* change to the input directory */
    if (chdir(input_dir) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    /* make sure we have the payload subdirectory */
    if (lstat(PAYLOAD_SUBDIR, &sb) == -1) {
        err(EXIT_FAILURE, "lstat");
    }

    if (!S_ISDIR(sb.st_mode)) {
        errx(EXIT_FAILURE, _("*** %s is not a directory"), PAYLOAD_SUBDIR);
    }

    /* read in signature.json and header.json */
    signature = read_json_file(OUTPUT_SIGNATURE);

    if (signature == NULL) {
        errx(EXIT_FAILURE, _("*** missing signature data"));
    }

    header = read_json_file(OUTPUT_HEADER);

    if (header == NULL) {
        errx(EXIT_FAILURE, _("*** missing header data"));
    }

    /* create the lead from header metadata */
    rawlead = create_lead(header);

    if (rawlead == NULL) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM lead"));
    }

    /* create the header (the main header) */
    if (create_header(header, &hdr, &hdrinfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM header"));
    }

    /* create the payload */
    payloadfd = create_payload(header, input_dir, PAYLOAD_SUBDIR);

    if (payloadfd == -1) {
        errx(EXIT_FAILURE, "create_payload");
    }

    /* create the signature */
    if (create_header(signature, &sig, &siginfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM signature"));
    }

    /* update the header payload digest (payload only) */
    if (update_header_digests(header, hdr, hdrinfo, payloadfd) != 0) {
        close(payloadfd);
        errx(EXIT_FAILURE, "update_header_digests");
    }

    /* free the old header structures before regenerating */
    free_header(hdr, hdrinfo);

    /* regenerate the header with updated digests */
    if (create_header(header, &hdr, &hdrinfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to reconstruct RPM header"));
    }

    /* recalculate the digests and update the signature data using the updated header */
    if (update_signature(signature, header, hdr, hdrinfo, payloadfd) != 0) {
        close(payloadfd);
        errx(EXIT_FAILURE, "update_signature");
    }

    /* free the old signature structures before regenerating */
    free_header(sig, siginfo);

    /* regenerate the signature with updated digests */
    if (create_header(signature, &sig, &siginfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to reconstruct RPM signature"));
    }

    /* create an RPM for writing */
    rpm = fopen(filename, "wb");

    if (rpm == NULL) {
        err(EXIT_FAILURE, "fopen");
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
    /* close the RPM */
    if (fclose(rpm) != 0) {
        warn("fclose");
    }

    /* back to the starting point and clean up */
    if (chdir(cwd) == -1) {
        err(EXIT_FAILURE, "chdir");
    }

    json_object_put(header);
    json_object_put(signature);
    free_header(sig, siginfo);
    free_header(hdr, hdrinfo);
    free(rawlead);

    return;
}
