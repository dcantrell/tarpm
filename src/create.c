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
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <ctype.h>
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
#include <lzma.h>

#include "tarpm.h"

static struct archive *payload = NULL;

/* bytes written to a stripped payload, which we have to pad by hand */
static size_t payload_offset = 0;

static void
free_header(struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo)
{
    free(hdr);

    if (hdrinfo) {
        free(hdrinfo->estart);
        free(hdrinfo->datastart);
        free(hdrinfo);
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
        return -1;
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
        /* create_header() always counts the trailer in nbytes */
        if (n < RPM_TRAILER_SIZE) {
            warnx(_("*** header data is too small to hold the region trailer"));
            return -1;
        }

        n -= RPM_TRAILER_SIZE;

        /* keep any write failure above */
        if (get_trailer_data(data, &trailer_data, &trailer_size, is_signature) == 0) {
            fix_trailer_offset(trailer_data, trailer_size, nentries);
        } else {
            r = -1;
        }
    }

    if (fwrite(hdrinfo->datastart, 1, n, rpm) != n) {
        warn("fwrite");
        r = -1;
    }

    /* write the trailer if present (for both signature and header sections) */
    if (r == 0 && trailer_size == RPM_TRAILER_SIZE) {
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
        path = xstrdup(basename);
    } else if (dirname[0] == '/') {
        xasprintf(&path, ".%s%s", dirname, basename);
    } else {
        xasprintf(&path, "./%s%s", dirname, basename);
    }

    return path;
}

/*
 * Write bytes to the payload and count them, so a stripped payload
 * knows where the next 4 byte boundary is.
 */
static int
write_payload_data(const void *buf, const size_t len)
{
    ssize_t written = 0;

    if (len == 0) {
        return 0;
    }

    written = archive_write_data(payload, buf, len);

    if (written < 0 || ((size_t) written) != len) {
        warnx("archive_write_data: %s", archive_error_string(payload));
        return -1;
    }

    payload_offset += len;
    return 0;
}

/* Pad a stripped payload out to the next 4 byte boundary */
static int
pad_payload(void)
{
    static const char zeros[CPIO_ALIGNMENT] = { 0 };
    int r = 0;

    r = write_payload_data(zeros, (CPIO_ALIGNMENT - (payload_offset % CPIO_ALIGNMENT)) % CPIO_ALIGNMENT);
    return r;
}

/*
 * Copy the contents of a file in the payload tree to the payload.  The
 * file has to hold exactly the size the header lists for it.  We open
 * it with open_beneath() so a symlink put in the tree after
 * check_payload_paths() ran is not followed.
 */
static int
write_file_data(const char *payload_dir, const char *relative_path, const uint64_t size)
{
    int fd = -1;
    int r = 0;
    ssize_t len = 0;
    uint64_t total = 0;
    char *file_path = NULL;
    char buf[BUFSIZ];

    if (payload_dir == NULL || relative_path == NULL) {
        return -1;
    }

    file_path = joinpath(payload_dir, relative_path, NULL);

    if ((fd = open_beneath(payload_dir, relative_path)) == -1) {
        if (errno == ELOOP || errno == EXDEV || errno == EINVAL) {
            warnx_escaped(_("*** %s leaves the payload tree or goes through a symlink in it"), relative_path);
        } else {
            warn_escaped("open: %s", file_path);
        }

        free(file_path);
        return -1;
    }

    while ((len = read(fd, buf, sizeof(buf))) > 0) {
        if (write_payload_data(buf, len) != 0) {
            r = -1;
            break;
        }

        total += len;
    }

    if (len == -1) {
        warn_escaped("read: %s", file_path);
        r = -1;
    } else if (r == 0 && total != size) {
        /* the file changed size after we scanned the payload tree */
        warnx_escaped(_("*** %s is %" PRIu64 " bytes but the header lists %" PRIu64), file_path, total, size);
        r = -1;
    }

    close(fd);
    free(file_path);
    return r;
}

/*
 * Write one file to a stripped payload the way
 * rpmfiArchiveWriteHeader() and iterWriteArchiveNext() in
 * lib/rpmfi.cc in the rpm source do.  The entry is the magic and the
 * file list index, then the file data or the symlink target.  The
 * rest of the metadata is in the RPM header.  A hardlink holds data
 * only in the last entry of its group, same as in a newc payload.
 */
static int
add_file_to_stripped_payload(const char *payload_dir, const struct file_params *params)
{
    int r = 0;
    char *relative_path = NULL;
    char entry[CPIO_STRIPPED_HEADER_SIZE + 1];

    if (payload_dir == NULL || params == NULL) {
        return -1;
    }

    snprintf(entry, sizeof(entry), "%s%08zx", CPIO_STRIPPED_MAGIC, params->index);

    if (pad_payload() != 0 || write_payload_data(entry, CPIO_STRIPPED_HEADER_SIZE) != 0 || pad_payload() != 0) {
        return -1;
    }

    if (S_ISLNK(params->mode)) {
        if (params->linkto != NULL) {
            r = write_payload_data(params->linkto, strlen(params->linkto));
        }
    } else if (S_ISREG(params->mode) && params->size > 0 && params->hardlink == NULL) {
        relative_path = joinpath(params->dirname, params->basename, NULL);
        r = write_file_data(payload_dir, relative_path, params->size);
        free(relative_path);
    }

    return r;
}

/*
 * End a stripped payload with the newc trailer rpmcpioTrailerWrite()
 * in lib/cpio.cc in the rpm source writes.  Every field is zero but
 * the link count and the name size.
 */
static int
write_stripped_trailer(void)
{
    char trailer[CPIO_NEWC_HEADER_SIZE + 1];

    snprintf(trailer, sizeof(trailer), "%s%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08zx%08x",
             CPIO_NEWC_MAGIC, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, sizeof(CPIO_TRAILER_NAME), 0);

    if (pad_payload() != 0 || write_payload_data(trailer, CPIO_NEWC_HEADER_SIZE) != 0) {
        return -1;
    }

    if (write_payload_data(CPIO_TRAILER_NAME, sizeof(CPIO_TRAILER_NAME)) != 0 || pad_payload() != 0) {
        return -1;
    }

    return 0;
}

/*
 * Helper function used to build the RPM payload.  We use libarchive,
 * but the payload has to follow the metadata in the header rather
 * than walking the filesystem tree.
 */
static int
add_file_to_payload(const char *payload_dir, const struct file_params *params)
{
    int r = 0;
    char *relative_path = NULL;
    char *full_path = NULL;
    struct archive_entry *entry = NULL;

    if (payload_dir == NULL || params == NULL) {
        return -1;
    }

    /* the relative path is where this file will live when installed */
    relative_path = joinpath(params->dirname, params->basename, NULL);

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
        r = -1;
        goto file_cleanup;
    }

    /* write the data to the payload for regular files but not hardlinks */
    if (S_ISREG(params->mode) && params->size > 0 && params->hardlink == NULL) {
        if (write_file_data(payload_dir, relative_path, params->size) != 0) {
            r = -1;
            goto file_cleanup;
        }
    }

    /* finalize the entry */
    if (archive_write_finish_entry(payload) != ARCHIVE_OK) {
        warnx("archive_write_finish_entry: %s", archive_error_string(payload));
        r = -1;
    }

file_cleanup:
    archive_entry_free(entry);
    free(full_path);
    free(relative_path);
    return r;
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
    int infd = -1;
    int outfd = -1;

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

    /* open file descriptors, a dup() that does not become a FILE * is closed below */
    infd = dup(uncompressed_fd);
    outfd = dup(compressed_fd);

    if (infd == -1 || outfd == -1) {
        warn("dup");
        rc = -1;
        goto cleanup;
    }

    in = fdopen(infd, "rb");

    if (in != NULL) {
        infd = -1;
    }

    out = fdopen(outfd, "wb");

    if (out != NULL) {
        outfd = -1;
    }

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

            if (fwrite(obuf, 1, zout.pos, out) != zout.pos) {
                warn("fwrite");
                rc = -1;
                goto cleanup;
            }

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

        if (zout.pos > 0 && fwrite(obuf, 1, zout.pos, out) != zout.pos) {
            warn("fwrite");
            rc = -1;
            goto cleanup;
        }
    } while (remaining != 0);

    /* fread() returns 0 on a read error as well as at the end */
    if (ferror(in)) {
        warnx(_("*** unable to read the uncompressed payload"));
        rc = -1;
    }

cleanup:
    if (cctx) {
        ZSTD_freeCCtx(cctx);
    }

    if (in) {
        fclose(in);
    }

    /* the final flush of the compressed payload can fail too */
    if (out && fclose(out) != 0) {
        warn("fclose");
        rc = -1;
    }

    if (infd != -1) {
        close(infd);
    }

    if (outfd != -1) {
        close(outfd);
    }

    free(ibuf);
    free(obuf);

    return rc;
}

/*
 * Compress a cpio archive with xz and a SHA-256 check.  xzdio in
 * rpmio/rpmio.c in the rpm source does the same through
 * lzma_easy_encoder().  libarchive always uses a CRC64 check, which
 * gives a payload rpmbuild would not write.
 *
 * Reads from uncompressed_fd and writes to compressed_fd.  Returns 0
 * on success, -1 on failure.
 */
static int
compress_with_xz_sha256(int uncompressed_fd, int compressed_fd, uint32_t level)
{
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_action action = LZMA_RUN;
    lzma_ret ret = LZMA_OK;
    uint8_t *ibuf = NULL;
    uint8_t *obuf = NULL;
    size_t n = 0;
    int rc = 0;
    int infd = -1;
    int outfd = -1;
    FILE *in = NULL;
    FILE *out = NULL;

    ret = lzma_easy_encoder(&strm, level, LZMA_CHECK_SHA256);

    if (ret != LZMA_OK) {
        warnx("lzma_easy_encoder: %d", (int) ret);
        return -1;
    }

    ibuf = xalloc(BUFSIZ);
    obuf = xalloc(BUFSIZ);

    /* open file descriptors, a dup() that does not become a FILE * is closed below */
    infd = dup(uncompressed_fd);
    outfd = dup(compressed_fd);

    if (infd == -1 || outfd == -1) {
        warn("dup");
        rc = -1;
        goto cleanup;
    }

    in = fdopen(infd, "rb");

    if (in != NULL) {
        infd = -1;
    }

    out = fdopen(outfd, "wb");

    if (out != NULL) {
        outfd = -1;
    }

    if (in == NULL || out == NULL) {
        warn("fdopen");
        rc = -1;
        goto cleanup;
    }

    strm.next_out = obuf;
    strm.avail_out = BUFSIZ;

    while (ret != LZMA_STREAM_END) {
        /* refill the input until the cpio stream ends */
        if (strm.avail_in == 0 && action == LZMA_RUN) {
            n = fread(ibuf, 1, BUFSIZ, in);

            /* fread() returns 0 on a read error as well as at the end */
            if (ferror(in)) {
                warnx(_("*** unable to read the uncompressed payload"));
                rc = -1;
                goto cleanup;
            }

            if (n == 0) {
                action = LZMA_FINISH;
            }

            strm.next_in = ibuf;
            strm.avail_in = n;
        }

        ret = lzma_code(&strm, action);

        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            warnx("lzma_code: %d", (int) ret);
            rc = -1;
            goto cleanup;
        }

        /* write out a full buffer and whatever is left at the end */
        if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
            n = BUFSIZ - strm.avail_out;

            if (n > 0 && fwrite(obuf, 1, n, out) != n) {
                warn("fwrite");
                rc = -1;
                goto cleanup;
            }

            strm.next_out = obuf;
            strm.avail_out = BUFSIZ;
        }
    }

cleanup:
    lzma_end(&strm);

    if (in) {
        fclose(in);
    }

    /* the final flush of the compressed payload can fail too */
    if (out && fclose(out) != 0) {
        warn("fclose");
        rc = -1;
    }

    if (infd != -1) {
        close(infd);
    }

    if (outfd != -1) {
        close(outfd);
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

    if (flags & RPMFILE_GHOST) {
        return true;
    } else {
        return false;
    }
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
            tagnum = rpmTagGetValue(str_or_empty(json_object_get_string(tagname)));

            if (json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
                if (tagnum == RPMTAG_BASENAMES) {
                    hfl->basenames = value;
                } else if (tagnum == RPMTAG_DIRNAMES) {
                    hfl->dirnames = value;
                } else if (tagnum == RPMTAG_DIRINDEXES) {
                    hfl->dirindexes = value;
                } else if (tagnum == RPMTAG_FILESIZES || tagnum == RPMTAG_LONGFILESIZES) {
                    hfl->filesizes = value;
                } else if (tagnum == RPMTAG_FILEMODES) {
                    hfl->filemodes = value;
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
 * Returns true if rpm would take the file list.  This checks what
 * indexSane() in lib/rpmfi.cc checks: the three list tags are there
 * together, they hold the same number of files, and every dirindex
 * names a real dirname.  A package with no files carries none of the
 * three and is fine.  Sets numfiles to the length of the list.
 */
static bool
valid_file_list(const struct hdr_file_lists *hfl, size_t *numfiles)
{
    size_t i = 0;
    size_t nbasenames = 0;
    size_t ndirnames = 0;
    int dirindex = 0;

    *numfiles = 0;

    if (hfl->basenames == NULL && hfl->dirnames == NULL && hfl->dirindexes == NULL) {
        return true;
    }

    if (hfl->basenames == NULL || hfl->dirnames == NULL || hfl->dirindexes == NULL) {
        return false;
    }

    if (json_object_get_type(hfl->basenames) != json_type_array || json_object_get_type(hfl->dirnames) != json_type_array || json_object_get_type(hfl->dirindexes) != json_type_array) {
        return false;
    }

    nbasenames = json_object_array_length(hfl->basenames);
    ndirnames = json_object_array_length(hfl->dirnames);

    if (json_object_array_length(hfl->dirindexes) != nbasenames) {
        return false;
    }

    for (i = 0; i < nbasenames; i++) {
        dirindex = json_object_get_int(json_object_array_get_idx(hfl->dirindexes, i));

        if (dirindex < 0 || (size_t) dirindex >= ndirnames) {
            return false;
        }
    }

    *numfiles = nbasenames;

    return true;
}

/* A payload file and its inode, sorted to find the hardlink groups */
struct inode_index {
    uint32_t inode;
    size_t index;
};

/* qsort() comparison by inode, then by place in the file list */
static int
compare_inode_index(const void *a, const void *b)
{
    const struct inode_index *x = a;
    const struct inode_index *y = b;

    if (x->inode < y->inode) {
        return -1;
    } else if (x->inode > y->inode) {
        return 1;
    } else if (x->index < y->index) {
        return -1;
    } else if (x->index > y->index) {
        return 1;
    }

    return 0;
}

/*
 * Find the hardlink groups in the file list.  RPM wants the last file
 * in a group to hold the data and the ones before it to link to it,
 * so we keep the last path we see for each group.  The groups are
 * numbered in the order they first show up in the file list and every
 * file gets its group number in hl->group.  We sort by inode so this
 * does not take time quadratic in the number of files.  We allocate
 * the arrays here and free_hardlink_groups() frees them.
 */
static void
find_hardlink_groups(const struct hdr_file_lists *hfl, const size_t numfiles, struct hardlink_groups *hl)
{
    size_t i = 0;
    size_t n = 0;
    size_t g = 0;
    size_t *first = NULL;
    size_t *last = NULL;
    uint16_t mode = 0;
    struct inode_index *sorted = NULL;

    if (hfl == NULL || hl == NULL) {
        return;
    }

    hl->paths = xcalloc(numfiles, sizeof(char *));
    hl->inodes = xcalloc(numfiles, sizeof(uint32_t));
    hl->nlinks = xcalloc(numfiles, sizeof(uint32_t));
    hl->group = xcalloc(numfiles, sizeof(size_t));
    sorted = xcalloc(numfiles, sizeof(*sorted));
    first = xcalloc(numfiles, sizeof(size_t));
    last = xcalloc(numfiles, sizeof(size_t));

    for (i = 0; i < numfiles; i++) {
        hl->group[i] = SIZE_MAX;
        first[i] = SIZE_MAX;
        mode = (uint16_t) json_object_get_int(json_object_array_get_idx(hfl->filemodes, i));

        /* only regular files can be hardlinks */
        if (!S_ISREG(mode)) {
            continue;
        }

        /* ghost files never make it in to the payload */
        if (is_ghost_file(hfl, i)) {
            continue;
        }

        sorted[n].inode = (uint32_t) json_object_get_int64(json_object_array_get_idx(hfl->fileinodes, i));
        sorted[n].index = i;
        n++;
    }

    if (n > 0) {
        qsort(sorted, n, sizeof(*sorted), compare_inode_index);
    }

    /* each run of one inode is a group and its first entry comes first in the file list */
    for (i = 0; i < n; i++) {
        if (i == 0 || sorted[i].inode != sorted[i - 1].inode) {
            first[sorted[i].index] = sorted[i].index;
        } else {
            first[sorted[i].index] = first[sorted[i - 1].index];
        }
    }

    /* number the groups in file list order */
    for (i = 0; i < numfiles; i++) {
        if (first[i] == SIZE_MAX) {
            continue;
        }

        if (first[i] == i) {
            g = hl->count;
            hl->inodes[g] = (uint32_t) json_object_get_int64(json_object_array_get_idx(hfl->fileinodes, i));
            hl->count++;
        } else {
            g = hl->group[first[i]];
        }

        hl->group[i] = g;
        hl->nlinks[g]++;
        last[g] = i;
    }

    for (g = 0; g < hl->count; g++) {
        hl->paths[g] = file_list_path(hfl, last[g]);
    }

    free(sorted);
    free(first);
    free(last);
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
    free(hl->group);

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
    size_t g = 0;
    size_t ordered = 0;
    size_t *next = NULL;

    if (hfl == NULL || hl == NULL || order == NULL) {
        return 0;
    }

    for (i = 0; i < numfiles; i++) {
        if (is_ghost_file(hfl, i)) {
            continue;
        }

        g = hl->group[i];

        if (g == SIZE_MAX || hl->nlinks[g] < 2) {
            order[ordered++] = i;
        }
    }

    /* each group gets a run of slots after the files with no group */
    next = xcalloc(hl->count + 1, sizeof(size_t));

    for (g = 0; g < hl->count; g++) {
        next[g] = ordered;

        if (hl->nlinks[g] > 1) {
            ordered += hl->nlinks[g];
        }
    }

    /* ghost files have no group, so they stay out */
    for (i = 0; i < numfiles; i++) {
        g = hl->group[i];

        if (g != SIZE_MAX && hl->nlinks[g] > 1) {
            order[next[g]++] = i;
        }
    }

    free(next);
    return ordered;
}

/* True if create_payload() can write a payload for this compressor */
static bool
known_compressor(const char *compressor)
{
    size_t i = 0;
    const char *names[] = { "gzip", "bzip2", "xz", "lzma", "zstd" };

    for (i = 0; i < (sizeof(names) / sizeof(names[0])); i++) {
        if (!strcmp(compressor, names[i])) {
            return true;
        }
    }

    return false;
}

/*
 * Parse Payloadflags the way rpm writes it, a compression level with an
 * optional T and thread count after it.  We only use the level.
 * Returns 0 and sets level, or -1 when the value is anything else.
 */
static int
parse_payload_flags(const char *flags, long *level)
{
    char *end = NULL;

    if (flags == NULL || level == NULL || !isdigit((unsigned char) *flags)) {
        return -1;
    }

    errno = 0;
    *level = strtol(flags, &end, 10);

    if (errno == ERANGE) {
        return -1;
    }

    if (*end == 'T') {
        end++;

        while (isdigit((unsigned char) *end)) {
            end++;
        }
    }

    if (*end != '\0') {
        return -1;
    }

    return 0;
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
    bool stripped = false;
    bool failed = false;
    size_t i = 0;
    size_t j = 0;
    size_t n = 0;
    size_t numfiles = 0;
    size_t ordered = 0;
    size_t *order = NULL;
    char *opts = NULL;
    char *current_path = NULL;
    const char *tag = NULL;
    const char *level = NULL;
    long flags_level = 0;
    struct hdr_file_lists hfl;
    struct hardlink_groups hl;
    struct zstd_payload zstd;
    struct file_params params;
    struct archive_entry *entry = NULL;
    struct json_object *tags = NULL;

    if (header == NULL || payload_dir == NULL) {
        return -1;
    }

    /* get the header tags array */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in header data"));
        return -1;
    }

    /* an unknown name would get an uncompressed payload */
    tag = get_tag_value(tags, RPMTAG_PAYLOADCOMPRESSOR, false);

    if (tag != NULL && !known_compressor(tag)) {
        warnx(_("*** unknown Payloadcompressor \"%s\", use gzip, bzip2, xz, lzma, zstd, or none"), tag);
        return -1;
    }

    /* initialize */
    memset(&hfl, '\0', sizeof(hfl));
    memset(&hl, '\0', sizeof(hl));
    memset(&zstd, '\0', sizeof(zstd));
    zstd.level = 3;

    /*
     * Get file lists from header.  create_rpm() generated these from
     * the "files" array with add_file_list_tags(), so anything wrong
     * here is a tarpm bug and not bad input.
     */
    collect_file_list_tags(tags, &hfl);

    if (!valid_file_list(&hfl, &numfiles)) {
        warnx(_("*** generated file list tags are inconsistent"));

        return -1;
    }

    if (numfiles > 0 && (!hfl.filesizes || !hfl.filemodes || !hfl.filerdevs || !hfl.filemtimes || !hfl.filelinktos || !hfl.fileinodes)) {
        warnx(_("*** missing generated file metadata tags in header data"));

        return -1;
    }

    /*
     * The payload goes in an anonymous file.  memfd_create() refuses a
     * name over 249 bytes, so it gets a fixed one and not the Name.
     */
    payloadfd = memfd_create("tarpm-payload", MFD_CLOEXEC);

    if (payloadfd == -1) {
        warn("memfd_create");

        return -1;
    }

    /* create a new payload writer */
    payload = archive_write_new();

    /*
     * rpm writes a stripped down cpio payload when a package has
     * Longfilesizes header tags.  A v6 format package always has them
     * and a v4 format package has them when a file is too big for
     * Filesizes.  libarchive cannot write this modified cpio format,
     * so we construct it manually and use libarchive's raw format to
     * still get compression via libarchive.  Everything else is in the
     * newc cpio format.
     */
    stripped = (rpmformat == RPM_FORMAT_V6);

    for (i = 0; !stripped && i < numfiles; i++) {
        if (json_object_get_int64(json_object_array_get_idx(hfl.filesizes, i)) >= (int64_t) UINT32_MAX) {
            stripped = true;
        }
    }

    payload_offset = 0;

    if (stripped) {
        if (archive_write_set_format_raw(payload) != ARCHIVE_OK) {
            warnx("archive_write_set_format_raw: %s", archive_error_string(payload));
            failed = true;
            goto close_payload;
        }
    } else if (archive_write_set_format_cpio_newc(payload) != ARCHIVE_OK) {
        warnx("archive_write_set_format_cpio_newc: %s", archive_error_string(payload));
        failed = true;
        goto close_payload;
    }

    /* get the compression algorithm type and level */
    tag = get_tag_value(tags, RPMTAG_PAYLOADCOMPRESSOR, false);

    /*
     * For zstd we compress by hand with no checksum.  The rpm cpio
     * reader cannot handle zstd frames with XXH64 checksums and
     * libarchive always adds them.  For xz we compress by hand with
     * the SHA-256 check rpm uses where libarchive writes CRC64.  So we
     * write a plain cpio first and compress it ourselves after.
     */
    if (tag != NULL && (!strcmp(tag, "zstd") || !strcmp(tag, "xz"))) {
        zstd.used = true;
        archive_write_add_filter_none(payload);

        /* liblzma reads level 6 as its default level */
        if (!strcmp(tag, "xz")) {
            zstd.xz = true;
            zstd.level = 6;
        }

        /*
         * The cpio stream handed to zstd or xz is not block padded, so
         * do not let libarchive pad out the final block.
         */
        if (archive_write_set_bytes_in_last_block(payload, 1) != ARCHIVE_OK) {
            warnx("archive_write_set_bytes_in_last_block: %s", archive_error_string(payload));
            failed = true;
            goto close_payload;
        }

        /* get the compression level */
        level = get_tag_value(tags, RPMTAG_PAYLOADFLAGS, false);

        if (level != NULL) {
            /* xz levels stop at 9, the same as libarchive takes */
            if (parse_payload_flags(level, &flags_level) != 0 || (zstd.xz && flags_level > 9)) {
                warnx_escaped(_("*** %s \"%s\" is not a compression level"), rpmTagGetName(RPMTAG_PAYLOADFLAGS), level);
                failed = true;
                goto close_payload;
            }

            /* zstd reads level 0 as its default level */
            if (flags_level > 22) {
                zstd.level = 22;
            } else {
                zstd.level = (int) flags_level;
            }
        }
    } else if (tag == NULL) {
        /* rpmbuild does not pad an uncompressed payload, so neither do we */
        archive_write_add_filter_none(payload);

        if (archive_write_set_bytes_in_last_block(payload, 1) != ARCHIVE_OK) {
            warnx("archive_write_set_bytes_in_last_block: %s", archive_error_string(payload));
            failed = true;
            goto close_payload;
        }
    } else if (!strcmp(tag, "gzip")) {
        archive_write_add_filter_gzip(payload);

        /*
         * rpm compresses through gzdopen() in zlib, which leaves the
         * gzip header mtime at 0.  libarchive writes the current time
         * there unless we turn off the timestamp option.
         */
        if (archive_write_set_filter_option(payload, "gzip", "timestamp", NULL) != ARCHIVE_OK) {
            warnx("archive_write_set_filter_option: %s", archive_error_string(payload));
            failed = true;
            goto close_payload;
        }
    } else if (!strcmp(tag, "bzip2")) {
        archive_write_add_filter_bzip2(payload);
    } else if (!strcmp(tag, "lzma")) {
        archive_write_add_filter_lzma(payload);
    }

    /* set the compression level for non-zstd compressors */
    if (!zstd.used && tag != NULL) {
        level = get_tag_value(tags, RPMTAG_PAYLOADFLAGS, false);

        if (level != NULL) {
            if (parse_payload_flags(level, &flags_level) != 0) {
                warnx_escaped(_("*** %s \"%s\" is not a compression level"), rpmTagGetName(RPMTAG_PAYLOADFLAGS), level);
                failed = true;
                goto close_payload;
            }

            xasprintf(&opts, "compression-level=%ld", flags_level);

            if (archive_write_set_options(payload, opts) != ARCHIVE_OK) {
                warnx("archive_write_set_options: %s", archive_error_string(payload));
                free(opts);
                failed = true;
                goto close_payload;
            }

            free(opts);
        }
    }

    /* open the payload for writing - use pipe for zstd and xz to avoid single-segment mode */
    if (zstd.used) {
        if (pipe(zstd.pipefd) == -1) {
            warn("pipe");
            failed = true;
            goto close_payload;
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

            failed = true;
            goto close_payload;
        } else if (zstd.pid == 0) {
            /* read from pipe, compress, write to payloadfd */
            if (close(zstd.pipefd[1]) == -1) {
                warn("close");
            }

            if (zstd.xz) {
                if (compress_with_xz_sha256(zstd.pipefd[0], payloadfd, (uint32_t) zstd.level) != 0) {
                    _exit(1);
                }
            } else if (compress_with_zstd_no_checksum(zstd.pipefd[0], payloadfd, zstd.level) != 0) {
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
        warnx("archive_write_open_fd: %s", archive_error_string(payload));
        failed = true;
        goto close_payload;
    }

    /* the raw format holds one regular file, which is our cpio stream */
    if (stripped) {
        entry = archive_entry_new();
        archive_entry_set_pathname(entry, "payload");
        archive_entry_set_filetype(entry, AE_IFREG);

        if (archive_write_header(payload, entry) != ARCHIVE_OK) {
            warnx("archive_write_header: %s", archive_error_string(payload));
            archive_entry_free(entry);
            failed = true;
            goto close_payload;
        }

        archive_entry_free(entry);
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
        params.mtime = (uint32_t) json_object_get_int64(json_object_array_get_idx(hfl.filemtimes, i));
        params.linkto = json_object_get_string(json_object_array_get_idx(hfl.filelinktos, i));

        /* the header names owners but has no ids, so every entry gets 0 */
        params.uid = 0;
        params.gid = 0;

        /*
         * every payload entry carries the inode number from the
         * header, but only regular files can be hardlinks
         */
        params.inode = (uint32_t) json_object_get_int64(json_object_array_get_idx(hfl.fileinodes, i));
        params.nlink = 1;
        params.hardlink = NULL;
        params.index = i;

        /* the group find_hardlink_groups() put this file in */
        j = hl.group[i];

        if (S_ISREG(params.mode) && j != SIZE_MAX) {
            params.nlink = hl.nlinks[j];

            /* build current file path */
            current_path = payload_path(params.dirname, params.basename);

            /* set hardlink target */
            if (strcmp(current_path, hl.paths[j]) != 0) {
                params.hardlink = hl.paths[j];
            }

            free(current_path);
        }

        /* the header lists every file, so a missing one is fatal */
        if (stripped) {
            if (add_file_to_stripped_payload(payload_dir, &params) != 0) {
                warnx_escaped("failed to add file: %s%s", params.dirname, params.basename);
                failed = true;
                break;
            }
        } else if (add_file_to_payload(payload_dir, &params) != 0) {
            warnx_escaped("failed to add file: %s%s", params.dirname, params.basename);
            failed = true;
            break;
        }
    }

    if (!failed && stripped && write_stripped_trailer() != 0) {
        warnx(_("*** unable to write the payload trailer"));
        failed = true;
    }

    /* close the payload so data can be flushed */
close_payload:
    if (archive_write_close(payload) != ARCHIVE_OK) {
        warnx("archive_write_close: %s", archive_error_string(payload));
        failed = true;
    }

    /* the error string goes with the archive, so there is none to print */
    if (archive_write_free(payload) != ARCHIVE_OK) {
        warnx("archive_write_free");
    }

    payload = NULL;

    /* clean up hardlink tracking */
    free_hardlink_groups(&hl);
    free(order);

    /* when using zstd or xz, close pipe and wait for compression child */
    if (zstd.used && zstd.pid > 0) {
        /* closing the pipe tells child EOF */
        if (close(tmp_payloadfd) == -1) {
            warn("close");
        }

        /* we cannot know how the child did without its status */
        if (waitpid(zstd.pid, &zstd.status, 0) == -1) {
            warn("waitpid");
            failed = true;
        } else if (!WIFEXITED(zstd.status) || WEXITSTATUS(zstd.status) != 0) {
            warnx(_("*** payload compression child failed"));
            failed = true;
        }
    }

    /* a payload missing anything does not match the header */
    if (failed) {
        close(payloadfd);
        return -1;
    }

    /* back to the beginning, the digests read from here */
    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
        close(payloadfd);
        return -1;
    }

    return payloadfd;
}

/*
 * Called by create_rpm() to copy the payload from the given file
 * descriptor to the current spot in the RPM file.
 *
 * NOTE:
 * This closes the file descriptor, which removes the temporary
 * payload file.  It does that on failure too.
 */
static int
write_payload(FILE *rpm, int fd)
{
    int r = 0;
    FILE *pload = NULL;
    char buf[BUFSIZ];
    size_t s = 0;

    if (rpm == NULL) {
        /* the payload file goes away on failure too */
        if (fd >= 0) {
            close(fd);
        }

        return -1;
    }

    /* bring the payload back to the beginning */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        warn("lseek");
        close(fd);
        return -1;
    }

    /* open the payload for reading */
    pload = fdopen(fd, "rb");

    if (pload == NULL) {
        warn("fdopen");
        close(fd);
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

    /* fread() returns 0 on a read error as well as at the end */
    if (r == 0 && ferror(pload)) {
        warn("fread");
        r = -1;
    }

    /* close the payload -- deletes the temporary file */
    if (fclose(pload) != 0) {
        warn("fclose");
        return -1;
    }

    return r;
}

/*
 * Rename a signature size tag to its 64-bit counterpart.  rpm does
 * this in rpmGenerateSignature() for a package too big for the 32-bit
 * tags to describe.  Returns non-zero on failure.
 */
static int
widen_size_tag(struct json_object *tags, const char *tag, const char *wide)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (entry == NULL) {
            break;
        }

        if (json_object_object_get_ex(entry, RPM_ENTRY_TAG_DESC, &value) == 1 && !strcmp(tag, str_or_empty(json_object_get_string(value)))) {
            json_object_object_add(entry, RPM_ENTRY_TAG_DESC, json_object_new_string(wide));
            json_object_object_add(entry, RPM_ENTRY_TYPE_DESC, json_object_new_string(strtagtype(RPM_INT64_TYPE)));
            return 0;
        }
    }

    return -1;
}

/*
 * Return the number of bytes the signature header uses minus the
 * padding that follows it, 0 on failure.
 */
static size_t
signature_size(struct json_object *signature)
{
    size_t size = 0;
    struct rpmhdr *sig = NULL;
    struct rpmhdrinfo *siginfo = NULL;

    if (create_header(signature, &sig, &siginfo, NULL, NULL, true) == -1) {
        return 0;
    }

    size = sizeof(*sig) + (ntohl(sig->nentries) * sizeof(struct rpmhdrentry)) + ntohl(sig->nbytes);
    free_header(sig, siginfo);
    return size;
}

/*
 * Return the reserved space tag in the signature header given an RPM
 * format version.
 */
static uint32_t
reserved_space_tag(const int format)
{
    if (format == RPM_FORMAT_V6) {
        return RPMSIGTAG_RESERVED_VALUE;
    }

    return RPMSIGTAG_RESERVEDSPACE;
}

/*
 * Remove bytes from the reserved space in the signature.  rpm does
 * this in rpmGenerateSignature() after moving to the 64-bit size
 * tags, and rpmsign(1) does it after adding signatures.  A header
 * cannot hold an empty tag, so the reserved space has to keep at
 * least one byte.
 *
 * Returns 0 on success, 1 if the bytes do not fit, and -1 on failure.
 */
static int
shrink_reserved_space(struct json_object *tags, const uint32_t tag, const size_t bytes)
{
    int r = 0;
    size_t size = 0;
    const char *old = NULL;
    char *value = NULL;
    void *blob = NULL;

    if (bytes == 0) {
        return 0;
    }

    old = get_tag_value(tags, tag, true);

    if (old == NULL || rpmBase64Decode(old, &blob, &size) != 0) {
        warnx(_("*** unable to read %s in signature"), sig_tag_name(tag));
        return -1;
    }

    free(blob);

    if (bytes >= size) {
        return 1;
    }

    blob = xcalloc(size - bytes, sizeof(char));
    value = rpmBase64Encode(blob, size - bytes, -1);
    free(blob);

    if (value == NULL) {
        warnx("rpmBase64Encode");
        return -1;
    }

    r = set_tag_value(tags, tag, true, value);
    free(value);
    return r;
}

/*
 * Take the per-file signatures out of the "files" array in the
 * header.json structure and add them to the signature.json structure.
 * tarpm puts these in header.json to make working with the metadata
 * more intuitive, but rpm(8) expects them in the signature header.
 *
 * rpmsign(1) adds these to a package that already exists and uses the
 * reserved space to write the data.  In most cases this does not grow
 * the signature, but if it overflows them rpmsign(1) grows the
 * signature header.  tarpm takes the same approach.
 *
 * Return 0 on success, non-zero on failure.
 */
static int
add_signature_files(struct json_object *signature, struct json_object *header, const char *payload_dir, const int format)
{
    size_t oldsize = 0;
    size_t newsize = 0;
    struct json_object *tags = NULL;

    if (json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in signature data"));
        return -1;
    }

    oldsize = signature_size(signature);
    add_file_signature_tags(signature, header, payload_dir, format);
    newsize = signature_size(signature);

    if (oldsize == 0 || newsize < oldsize || shrink_reserved_space(tags, reserved_space_tag(format), newsize - oldsize) == -1) {
        warnx(_("*** failed to resize the reserved space in signature"));
        return -1;
    }

    return 0;
}

/*
 * Read signature.json for -D and -k, from the -S path when given and
 * from the input directory otherwise.  A missing signature.json in the input
 * directory is not an error, we compute every digest then.  Sets kept
 * to the allocated JSON or NULL and returns non-zero on failure.
 */
static int
read_kept_signature(const struct json_paths *paths, struct json_object **kept)
{
    const char *path = OUTPUT_SIGNATURE;

    *kept = NULL;

    if (!keep_digests && !keep_signatures) {
        return 0;
    }

    if (paths != NULL && paths->signature != NULL) {
        path = paths->signature;
    } else if (access(path, F_OK) == -1) {
        return 0;
    }

    *kept = read_json_file(path);

    if (*kept == NULL) {
        warnx_escaped(_("*** unable to read %s"), path);
        return -1;
    }

    return 0;
}

/*
 * Returns true if a digest from signature.json has the length rpm
 * expects for the tag.  Md5 is base64 for 16 bytes, Sha1 is 40 hex
 * digits, Sha256 and Sha3_256 are 64.  Any other length changes the
 * size of the signature header and rpm reports it as BAD.
 */
static bool
kept_digest_fits(const uint32_t tag, const char *value)
{
    bool r = false;
    uint8_t *blob = NULL;
    size_t blobsize = 0;

    if (tag == RPMSIGTAG_MD5) {
        if (rpmBase64Decode(value, (void **) &blob, &blobsize) == 0 && blobsize == MD5_DIGEST_LENGTH) {
            r = true;
        }

        free(blob);
    } else if (tag == RPMSIGTAG_SHA1) {
        r = (strlen(value) == SHA_DIGEST_LENGTH * 2);
    } else {
        r = (strlen(value) == SHA256_DIGEST_LENGTH * 2);
    }

    return r;
}

/*
 * Replace the signature digests we computed with the ones signature.json
 * carries.  Only the digests the format put in the signature header
 * are replaced, so a v4 format Md5 does not land in a v6 format
 * signature.  A kept digest of the wrong length is left out and we
 * keep the computed one.  Returns non-zero on failure.
 */
static int
keep_signature_digests(struct json_object *tags, struct json_object *kept)
{
    size_t i = 0;
    const char *name = NULL;
    const char *value = NULL;
    struct json_object *kepttags = NULL;
    const uint32_t digests[] = { RPMSIGTAG_MD5, RPMSIGTAG_SHA1, RPMSIGTAG_SHA256, RPMSIGTAG_SHA3_256_VALUE, 0 };

    /* -k reads signature.json too, so kept alone does not mean -D */
    if (!keep_digests || kept == NULL) {
        return 0;
    }

    if (json_object_object_get_ex(kept, RPM_ENTRY_TAGS_DESC, &kepttags) == 0) {
        warnx(_("*** missing tags in signature.json"));
        return -1;
    }

    for (i = 0; digests[i] != 0; i++) {
        name = sig_tag_name(digests[i]);

        if (get_tag_value(tags, digests[i], true) == NULL) {
            continue;
        }

        value = get_tag_value(kepttags, digests[i], true);

        if (value == NULL || value[0] == '\0') {
            continue;
        }

        if (!kept_digest_fits(digests[i], value)) {
            warnx(_("*** %s in signature.json has the wrong length, computing it instead"), name);
            continue;
        }

        if (set_tag_value(tags, digests[i], true, value) != 0) {
            warnx(_("*** failed to update %s in signature"), name);
            return -1;
        }
    }

    return 0;
}

/*
 * Check a package signature value from signature.json and return a new
 * signature header entry for it with the type rpm uses for the tag.
 * Openpgp is an array of non-empty strings, the others are base64
 * strings.  Returns NULL on failure.
 */
static struct json_object *
kept_signature_entry(const uint32_t tag, struct json_object *value)
{
    size_t i = 0;
    uint8_t *blob = NULL;
    size_t blobsize = 0;
    struct json_object *element = NULL;
    struct json_object *copy = NULL;

    if (tag == RPMSIGTAG_OPENPGP_VALUE) {
        if (json_object_get_type(value) != json_type_array) {
            warnx(_("*** the %s value in signature.json is not an array of strings"), sig_tag_name(tag));
            return NULL;
        }

        for (i = 0; i < json_object_array_length(value); i++) {
            element = json_object_array_get_idx(value, i);

            if (json_object_get_type(element) != json_type_string || json_object_get_string_len(element) == 0) {
                warnx(_("*** the %s value in signature.json is not an array of strings"), sig_tag_name(tag));
                return NULL;
            }
        }
    } else {
        if (json_object_get_type(value) != json_type_string || rpmBase64Decode(json_object_get_string(value), (void **) &blob, &blobsize) != 0) {
            warnx(_("*** the %s value in signature.json is not base64 data"), sig_tag_name(tag));
            return NULL;
        }

        free(blob);
    }

    if (json_object_deep_copy(value, &copy, NULL) != 0) {
        warnx(_("*** unable to copy %s from signature.json"), sig_tag_name(tag));
        return NULL;
    }

    if (tag == RPMSIGTAG_OPENPGP_VALUE) {
        return new_signature_entry(tag, RPM_STRING_ARRAY_TYPE, copy);
    }

    return new_signature_entry(tag, RPM_BIN_TYPE, copy);
}

/*
 * Returns true if rpm reads a package signature tag in the format.
 * rpm reads Rsa and Dsa in a v6 format package and rpmsign(1) writes
 * Rsa there too.  Pgp, Gpg and Pgp5 are ignored in that format.
 */
static bool
format_reads_signature(const uint32_t tag, const int format)
{
    if (format == RPM_FORMAT_V6 && (tag == RPMSIGTAG_PGP || tag == RPMSIGTAG_GPG || tag == RPMSIGTAG_PGP5)) {
        return false;
    }

    return true;
}

/*
 * Copy the package signatures signature.json carries in to the
 * signature header for -k.  These are the rpmsign(1) tags other than
 * the file and verity signatures, which come from header.json.  An
 * entry with no value is left out, as is a tag we already have and
 * one the format does not read.  Each entry is rebuilt with the type
 * rpm uses for the tag.  The room they take comes out of the reserved
 * space, the way rpmsign(1) adds them.  Returns non-zero on failure.
 */
static int
keep_package_signatures(struct json_object *signature, struct json_object *kept, const int format)
{
    size_t i = 0;
    size_t oldsize = 0;
    size_t newsize = 0;
    uint32_t tag = 0;
    struct json_object *tags = NULL;
    struct json_object *kepttags = NULL;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;
    struct json_object *copy = NULL;

    if (!keep_signatures || kept == NULL) {
        return 0;
    }

    if (json_object_object_get_ex(signature, RPM_ENTRY_TAGS_DESC, &tags) == 0) {
        warnx(_("*** missing tags in signature data"));
        return -1;
    }

    if (json_object_object_get_ex(kept, RPM_ENTRY_TAGS_DESC, &kepttags) == 0) {
        warnx(_("*** missing tags in signature.json"));
        return -1;
    }

    oldsize = signature_size(signature);

    for (i = 0; i < json_object_array_length(kepttags); i++) {
        entry = json_object_array_get_idx(kepttags, i);
        tag = get_tag_number(entry, true);

        if (!is_rpmsign_tag(tag) || is_file_signature_tag(tag)) {
            continue;
        }

        if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value) || value == NULL) {
            continue;
        }

        /* Openpgp is a string array, the others are base64 strings */
        if (json_object_get_type(value) == json_type_array) {
            if (json_object_array_length(value) == 0) {
                continue;
            }
        } else if (json_object_get_type(value) == json_type_string && json_object_get_string_len(value) == 0) {
            continue;
        }

        if (!format_reads_signature(tag, format)) {
            warnx(_("*** leaving %s out of the package, rpm does not read it in the v%d format"), sig_tag_name(tag), format);
            continue;
        }

        if (has_tag(tags, tag, true)) {
            warnx(_("*** signature.json has more than one %s, keeping the first"), sig_tag_name(tag));
            continue;
        }

        /* only the value comes from signature.json */
        copy = kept_signature_entry(tag, value);

        if (copy == NULL) {
            return -1;
        }

        json_object_array_add(tags, copy);
    }

    newsize = signature_size(signature);

    if (oldsize == 0 || newsize < oldsize || shrink_reserved_space(tags, reserved_space_tag(format), newsize - oldsize) == -1) {
        warnx(_("*** failed to resize the reserved space in signature"));
        return -1;
    }

    return 0;
}

/*
 * Warn when -k keeps package signatures for a main header that is not
 * the one they signed.  The signatures cover the main header, so we
 * compare the Sha256 of the header we wrote with the one signature.json
 * carries.  A signature.json with no Sha256 or no package signatures
 * gets no warning.
 */
static void
check_kept_signatures(struct json_object *kept, const char *sha256)
{
    size_t i = 0;
    uint32_t tag = 0;
    bool signed_package = false;
    const char *value = NULL;
    struct json_object *kepttags = NULL;
    struct json_object *entry = NULL;
    struct json_object *sig = NULL;

    if (!keep_signatures || kept == NULL || sha256 == NULL) {
        return;
    }

    if (json_object_object_get_ex(kept, RPM_ENTRY_TAGS_DESC, &kepttags) == 0 || json_object_get_type(kepttags) != json_type_array) {
        return;
    }

    for (i = 0; i < json_object_array_length(kepttags); i++) {
        entry = json_object_array_get_idx(kepttags, i);
        tag = get_tag_number(entry, true);

        if (!is_rpmsign_tag(tag) || is_file_signature_tag(tag)) {
            continue;
        }

        if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &sig) || sig == NULL) {
            continue;
        }

        if ((json_object_get_type(sig) == json_type_array && json_object_array_length(sig) > 0) || (json_object_get_type(sig) == json_type_string && json_object_get_string_len(sig) > 0)) {
            signed_package = true;
            break;
        }
    }

    value = get_tag_value(kepttags, RPMSIGTAG_SHA256, true);

    if (signed_package && value != NULL && strcasecmp(value, sha256) != 0) {
        warnx(_("*** the main header differs from the one signature.json was signed for, the kept signatures will not verify"));
    }

    return;
}

/*
 * Update the digests and sizes in the signature using the header and
 * payload data.  archivesize is the uncompressed payload size that
 * update_payload_tags() measured.  A digest kept carries goes in
 * place of the computed one.  Returns non-zero on failure.
 */
static int
update_signature(struct json_object *signature, struct json_object *header, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const int payloadfd, const int format, struct json_object *kept, const uint64_t archivesize)
{
    int i = 0;
    unsigned char *digest = NULL;
    void *blob = NULL;
    char *buf = NULL;
    uint32_t sizetag = 0;
    uint32_t payloadtag = 0;
    struct json_object *tags = NULL;
    off_t payload_off = 0;
    uint64_t payloadsize = 0;
    uint64_t totalsize = 0;
    uint64_t hdrsize = 0;
    uint64_t nentries = 0;
    uint64_t nbytes = 0;
    size_t oldsize = 0;
    size_t newsize = 0;

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

    /*
     * The sizes and the MD5 and SHA-1 digests only belong in a v4
     * signature header.  The v6 one carries digests of the main
     * header and nothing else.
     */
    if (format == RPM_FORMAT_V4) {
        /* a package too big for the 32-bit tags gets the 64-bit ones */
        if (totalsize >= UINT32_MAX || archivesize >= UINT32_MAX) {
            sizetag = RPMSIGTAG_LONGSIZE;
            payloadtag = RPMSIGTAG_LONGARCHIVESIZE;
            oldsize = signature_size(signature);

            if (widen_size_tag(tags, sig_tag_name(RPMSIGTAG_SIZE), sig_tag_name(sizetag)) != 0 || widen_size_tag(tags, sig_tag_name(RPMSIGTAG_PAYLOADSIZE), sig_tag_name(payloadtag)) != 0) {
                warnx(_("*** failed to widen the size tags in signature"));
                return -1;
            }

            /* the 64-bit tags come out of the reserved space */
            newsize = signature_size(signature);

            if (oldsize == 0 || newsize < oldsize || shrink_reserved_space(tags, reserved_space_tag(format), newsize - oldsize) != 0) {
                warnx(_("*** failed to resize the reserved space in signature"));
                return -1;
            }
        } else {
            sizetag = RPMSIGTAG_SIZE;
            payloadtag = RPMSIGTAG_PAYLOADSIZE;
        }

        /* update the size tag (header + payload) */
        xasprintf(&buf, "%" PRIu64, totalsize);

        if (set_tag_value(tags, sizetag, true, buf) != 0) {
            warnx(_("*** failed to update %s in signature"), sig_tag_name(sizetag));
            free(buf);
            return -1;
        }

        free(buf);

        /* update the payload size tag */
        xasprintf(&buf, "%" PRIu64, archivesize);

        if (set_tag_value(tags, payloadtag, true, buf) != 0) {
            warnx(_("*** failed to update %s in signature"), sig_tag_name(payloadtag));
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

        if (set_tag_value(tags, RPMSIGTAG_MD5, true, buf) != 0) {
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

        if (set_tag_value(tags, RPMSIGTAG_SHA1, true, buf) != 0) {
            warnx(_("*** failed to update SHA-1 digest in signature"));
            free(digest);
            free(buf);
            return -1;
        }

        free(digest);
        free(buf);
    }

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

    if (set_tag_value(tags, RPMSIGTAG_SHA256, true, buf) != 0) {
        warnx(_("*** failed to update SHA-256 digest in signature"));
        free(digest);
        free(buf);
        return -1;
    }

    check_kept_signatures(kept, buf);

    free(digest);
    free(buf);

    /* the SHA3-256 digest is only in a v6 signature header */
    if (format == RPM_FORMAT_V6) {
        digest = mksigdigest(TARPM_DIGEST_SHA3_256, hdr, hdrinfo, header, payloadfd);

        if (digest == NULL) {
            warnx(_("*** failed to compute the signature SHA3-256 digest"));
            return -1;
        }

        buf = xcalloc(SHA256_DIGEST_LENGTH * 2 + 1, sizeof(char));

        for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            sprintf(&buf[i * 2], "%02x", (unsigned int) digest[i]);
        }

        if (set_tag_value(tags, RPMSIGTAG_SHA3_256_VALUE, true, buf) != 0) {
            warnx(_("*** failed to update SHA3-256 digest in signature"));
            free(digest);
            free(buf);
            return -1;
        }

        free(digest);
        free(buf);
    }

    /* with -D the digests in signature.json win */
    if (keep_signature_digests(tags, kept) != 0) {
        return -1;
    }

    return 0;
}

/*
 * Warn about a Name, Version or Release holding a slash.  rpmbuild
 * refuses those, and tarpm -x will not extract the package without
 * -O since the NEVRA names the output directory.
 */
static void
warn_nevr_slash(struct json_object *header)
{
    size_t i = 0;
    const char *value = NULL;
    struct json_object *tags = NULL;
    const rpmTagVal nevr[] = { RPMTAG_NAME, RPMTAG_VERSION, RPMTAG_RELEASE, 0 };

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        return;
    }

    for (i = 0; nevr[i] != 0; i++) {
        value = get_tag_value(tags, nevr[i], false);

        if (value != NULL && strchr(value, '/') != NULL) {
            warnx(_("*** %s \"%s\" holds a slash, rpmbuild would refuse it"), rpmTagGetName(nevr[i]), value);
        }
    }

    return;
}

/*
 * Safety function to open the file we write the package to.  tmpname
 * is the temporary file created.  The caller can rename or remove it,
 * but must free the string.  Returns NULL on failure.
 */
static FILE *
open_package(const char *filename, char **tmpname)
{
    int fd = -1;
    bool exists = false;
    char *dir = NULL;
    mode_t mask = 0;
    mode_t mode = 0;
    struct stat sb;
    FILE *rpm = NULL;

    if (stat(filename, &sb) == 0) {
        exists = true;
    } else if (errno != ENOENT) {
        warn("stat");
        return NULL;
    }

    if (exists && !S_ISREG(sb.st_mode)) {
        rpm = fopen(filename, "wb");

        if (rpm == NULL) {
            warn("fopen");
        }

        return rpm;
    }

    /* rename() would get past a file we cannot write to */
    if (exists && access(filename, W_OK) == -1) {
        warn("%s", filename);
        return NULL;
    }

    dir = xdirname(filename);

    if (dir == NULL) {
        warnx(_("*** unable to find the directory of %s"), filename);
        return NULL;
    }

    xasprintf(tmpname, "%s/.%s-XXXXXX", dir, COMMAND_NAME);
    free(dir);
    fd = mkstemp(*tmpname);

    if (fd == -1) {
        warn("*** mkstemp %s", *tmpname);
        free(*tmpname);
        *tmpname = NULL;
        return NULL;
    }

    if (exists) {
        mode = sb.st_mode & 07777;

        /* only root can give a file away, others end up owning the new one */
        if (fchown(fd, sb.st_uid, sb.st_gid) == -1 && errno != EPERM) {
            warn("fchown");
        }
    } else {
        mask = umask(0);
        umask(mask);
        mode = 0666 & ~mask;
    }

    if (fchmod(fd, mode) == -1) {
        warn("fchmod");
    }

    rpm = fdopen(fd, "wb");

    if (rpm == NULL) {
        warn("fdopen");
        close(fd);

        if (unlink(*tmpname) == -1) {
            warn("unlink");
        }

        free(*tmpname);
        *tmpname = NULL;
    }

    return rpm;
}

/*
 * Return how many values a tag in a 'tags' array holds, which is zero
 * for a tag that is not there.
 */
static size_t
raw_tag_count(struct json_object *tags, const rpmTagVal tag)
{
    size_t i = 0;
    struct json_object *entry = NULL;
    struct json_object *value = NULL;

    for (i = 0; i < json_object_array_length(tags); i++) {
        entry = json_object_array_get_idx(tags, i);

        if (get_tag_number(entry, false) != tag) {
            continue;
        }

        if (!json_object_object_get_ex(entry, RPM_ENTRY_VALUE_DESC, &value)) {
            return 0;
        }

        if (json_object_get_type(value) == json_type_array) {
            return json_object_array_length(value);
        }

        return 1;
    }

    return 0;
}

/*
 * Check the file list and changelog tags header.json carries raw, as
 * an extract does when the dedicated arrays cannot be built.  We make
 * the file list tags ourselves, so raw ones would end up in the header
 * twice.  The changelog tags go in as they are, so they have to agree.
 * Returns 0 if the tags are fine, -1 otherwise.
 */
static int
check_raw_tags(struct json_object *header)
{
    size_t i = 0;
    size_t ntimes = 0;
    rpmTagVal tag = 0;
    struct json_object *tags = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) || json_object_get_type(tags) != json_type_array) {
        return 0;
    }

    for (i = 0; i < json_object_array_length(tags); i++) {
        tag = get_tag_number(json_object_array_get_idx(tags, i), false);

        if (is_file_list_tag(tag)) {
            warnx(_("*** remove %s from the tags in %s, tarpm builds the file list tags"), rpmTagGetName(tag), OUTPUT_HEADER);
            return -1;
        }
    }

    ntimes = raw_tag_count(tags, RPMTAG_CHANGELOGTIME);

    if (ntimes != raw_tag_count(tags, RPMTAG_CHANGELOGNAME) || ntimes != raw_tag_count(tags, RPMTAG_CHANGELOGTEXT)) {
        warnx(_("*** changelog tags in %s have mismatched lengths"), OUTPUT_HEADER);
        return -1;
    }

    return 0;
}

/* qsort() comparison for tag numbers */
static int
compare_tag_value(const void *a, const void *b)
{
    rpmTagVal x = *(const rpmTagVal *) a;
    rpmTagVal y = *(const rpmTagVal *) b;

    return (x > y) - (x < y);
}

/*
 * Warn about a tag that header.json carries more than once.  Names
 * in another case or the numeric form count as the same tag.  The
 * lookups only see the first copy.  A package can carry a tag twice
 * and an extract keeps both, so this is not an error.
 */
static void
warn_duplicate_tags(struct json_object *header)
{
    size_t i = 0;
    size_t n = 0;
    const char *name = NULL;
    rpmTagVal *numbers = NULL;
    struct json_object *tags = NULL;

    if (!json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags) || json_object_get_type(tags) != json_type_array) {
        return;
    }

    n = json_object_array_length(tags);

    if (n < 2) {
        return;
    }

    numbers = xcalloc(n, sizeof(*numbers));

    for (i = 0; i < n; i++) {
        numbers[i] = get_tag_number(json_object_array_get_idx(tags, i), false);
    }

    qsort(numbers, n, sizeof(*numbers), compare_tag_value);

    /* warn once for each tag, on its second copy */
    for (i = 1; i < n; i++) {
        if (numbers[i] == RPMTAG_NOT_FOUND || numbers[i] != numbers[i - 1] || (i > 1 && numbers[i] == numbers[i - 2])) {
            continue;
        }

        name = rpmTagGetName(numbers[i]);

        if (!strcmp(name, "(unknown)")) {
            warnx(_("*** tag #%d is in the tags in %s more than once"), numbers[i], OUTPUT_HEADER);
        } else {
            warnx(_("*** %s is in the tags in %s more than once"), name, OUTPUT_HEADER);
        }
    }

    free(numbers);
    return;
}

/*
 * Check that every path in the "files" array stays in the payload
 * tree.  The path is joined to payload_dir to find the file, so ".."
 * or a symlinked directory in the tree would let a crafted header.json
 * put any file we can read in to the package.  Every bad path gets a
 * warning.  Returns 0 if all of them are fine, -1 otherwise.
 */
static int
check_payload_paths(struct json_object *header, const char *payload_dir)
{
    int r = 0;
    size_t i = 0;
    const char *path = NULL;
    struct json_object *files = NULL;
    struct json_object *value = NULL;

    if (!json_object_object_get_ex(header, RPM_FILES_DESC, &files) || json_object_get_type(files) != json_type_array) {
        return 0;
    }

    for (i = 0; i < json_object_array_length(files); i++) {
        if (!json_object_object_get_ex(json_object_array_get_idx(files, i), RPM_FILE_PATH_DESC, &value)) {
            continue;
        }

        path = json_object_get_string(value);

        if (path == NULL) {
            continue;
        }

        if (!path_beneath(payload_dir, path, true)) {
            warnx_escaped(_("*** %s leaves the payload tree or goes through a symlink in it"), path);
            r = -1;
        }
    }

    return r;
}

/* Handler for -c mode (create) */
int
create_rpm(const char *filename, const char *cwd, const char *input_dir, const struct json_paths *paths)
{
    int r = 0;
    int rc = -1;
    FILE *rpm = NULL;
    int payloadfd = -1;
    uint32_t keptpayload = 0;
    uint64_t archivesize = 0;
    struct stat sb;
    char *tmpname = NULL;
    char *header_dir = NULL;
    char *payload_dir = NULL;
    const char *header_path = OUTPUT_HEADER;
    struct json_object *signature = NULL;
    struct json_object *keptsig = NULL;
    struct json_object *header = NULL;
    struct json_object *tags = NULL;
    struct json_object *files = NULL;
    struct json_object *dependencies = NULL;
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

    /* change to the input directory, every failure after this goes through create_cleanup */
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
        goto create_cleanup;
    }

    /* make sure we have the payload subdirectory */
    if (lstat(payload_dir, &sb) == -1) {
        warn("lstat");
        goto create_cleanup;
    }

    if (!S_ISDIR(sb.st_mode)) {
        warn(_("*** %s is not a directory"), payload_dir);
        goto create_cleanup;
    }

    /* the caller may have named where the JSON metadata files are */
    if (paths != NULL && paths->header != NULL) {
        header_path = paths->header;

        /* tag values kept in their own file sit next to header.json */
        header_dir = xdirname(header_path);
    } else {
        header_dir = strdup(input_dir);
    }

    if (header_dir == NULL) {
        warnx(_("*** unable to set header_dir"));
        goto create_cleanup;
    }

    /*
     * Build the signature header ourselves.  Everything in it is
     * recalculated from the header and the payload.  Only -D and -k
     * read signature.json, for the digests and signatures it carries.
     */
    signature = make_signature(rpmformat);

    if (signature == NULL) {
        warnx(_("*** unable to construct RPM signature"));
        goto create_cleanup;
    }

    /* read in header.json */
    header = read_json_file(header_path);

    if (header == NULL) {
        warnx(_("*** missing header data"));
        goto create_cleanup;
    }

    warn_duplicate_tags(header);

    /* everything below reads the dependencies, so check them first */
    if (!json_object_object_get_ex(header, RPM_DEPENDENCIES_DESC, &dependencies)) {
        dependencies = NULL;
    }

    /*
     * Fill in the tags we can work out ourselves.  This runs first
     * because the payload scan below goes by the digest algorithm and
     * the package type it leaves behind.
     */
    if (check_dependencies(dependencies) != 0 || apply_header_defaults(header, rpmformat) != 0) {
        goto create_cleanup;
    }

    warn_nevr_slash(header);

    if (check_raw_tags(header) != 0) {
        goto create_cleanup;
    }

    /* pick up the files added to the payload tree */
    if (json_object_object_get_ex(header, RPM_ENTRY_TAGS_DESC, &tags)) {
        /* a tree naming no files gets a list built from the payload */
        if (!json_object_object_get_ex(header, RPM_FILES_DESC, &files)) {
            files = json_object_new_array();
            json_object_object_add(header, RPM_FILES_DESC, files);
        }

        add_payload_files(tags, files, payload_dir);
    }

    if (check_payload_paths(header, payload_dir) != 0) {
        goto create_cleanup;
    }

    /* note the payload digests header.json carries before placeholders go in */
    keptpayload = kept_payload_digests(header);

    /*
     * Put the main header in to the shape the format calls for before
     * anything reads it.  The lead, the header and the payload all
     * depend on what this leaves behind.
     */
    if (apply_rpmformat(header, rpmformat) != 0) {
        warnx(_("*** unable to apply the RPM format to the header"));
        goto create_cleanup;
    }

    /* create the lead from header metadata */
    rawlead = create_lead(header);

    if (rawlead == NULL) {
        warnx(_("*** unable to construct RPM lead"));
        goto create_cleanup;
    }

    /*
     * Build the file list tags once.  Both header passes and the
     * payload read them, and building them again would repeat the
     * digest and libmagic work and the warnings.  The dependencies go
     * in because the depends dictionary holds indexes in to them.
     */
    if (files != NULL && add_file_list_tags(tags, files, payload_dir, dependencies, rpmformat) != 0) {
        goto create_cleanup;
    }

    /* create the header (the main header) */
    if (create_header(header, &hdr, &hdrinfo, payload_dir, header_dir, false) == -1) {
        warnx(_("*** unable to construct RPM header"));
        goto create_cleanup;
    }

    /* create the payload */
    payloadfd = create_payload(header, payload_dir);

    if (payloadfd == -1) {
        warnx("create_payload");
        goto create_cleanup;
    }

    /* create the signature */
    if (create_header(signature, &sig, &siginfo, NULL, NULL, true) == -1) {
        warnx(_("*** unable to construct RPM signature"));
        goto create_cleanup;
    }

    /* work out the payload digests and sizes the format calls for */
    if (update_payload_tags(header, payloadfd, rpmformat, keptpayload, &archivesize) != 0) {
        warnx("update_payload_tags");
        goto create_cleanup;
    }

    /* free the old header structures before regenerating */
    free_header(hdr, hdrinfo);
    hdr = NULL;
    hdrinfo = NULL;

    /* regenerate the header with updated digests */
    if (create_header(header, &hdr, &hdrinfo, payload_dir, header_dir, false) == -1) {
        warnx(_("*** unable to reconstruct RPM header"));
        goto create_cleanup;
    }

    /* with -D and -k what signature.json carries goes in as-is */
    if (read_kept_signature(paths, &keptsig) != 0) {
        goto create_cleanup;
    }

    /* recalculate the digests and update the signature data using the updated header */
    if (update_signature(signature, header, hdr, hdrinfo, payloadfd, rpmformat, keptsig, archivesize) != 0) {
        warnx("update_signature");
        goto create_cleanup;
    }

    /*
     * The file and verity signatures go in last, the way rpmsign(1)
     * adds them to a package rpmbuild(1) wrote.  The header passes
     * above dropped them from the files that changed.
     */
    if (add_signature_files(signature, header, payload_dir, rpmformat) != 0) {
        goto create_cleanup;
    }

    /* with -k the package signatures from signature.json go in too */
    if (keep_package_signatures(signature, keptsig, rpmformat) != 0) {
        goto create_cleanup;
    }

    /* free the old signature structures before regenerating */
    free_header(sig, siginfo);
    sig = NULL;
    siginfo = NULL;

    /* regenerate the signature with updated digests */
    if (create_header(signature, &sig, &siginfo, NULL, NULL, true) == -1) {
        warnx(_("*** unable to reconstruct RPM signature"));
        goto create_cleanup;
    }

    /* create an RPM for writing */
    rpm = open_package(filename, &tmpname);

    if (rpm == NULL) {
        goto create_cleanup;
    }

    /* write the lead to the RPM */
    if (fwrite(rawlead, sizeof(*rawlead), 1, rpm) != 1) {
        warn("fwrite");
        goto create_cleanup;
    }

    /* write the signature to the RPM */
    if (write_header(rpm, sig, siginfo, true, signature) == -1) {
        goto create_cleanup;
    }

    /* write the header to the RPM */
    if (write_header(rpm, hdr, hdrinfo, false, header) == -1) {
        goto create_cleanup;
    }

    /* write_payload() closes the payload whether or not it works */
    r = write_payload(rpm, payloadfd);
    payloadfd = -1;

    if (r == -1) {
        goto create_cleanup;
    }

    rc = 0;

create_cleanup:
    json_object_put(header);
    json_object_put(signature);
    json_object_put(keptsig);
    free_header(sig, siginfo);
    free_header(hdr, hdrinfo);
    free(rawlead);
    free(header_dir);
    free(payload_dir);

    if (payloadfd != -1 && close(payloadfd) == -1) {
        warn("close");
    }

    /* close the RPM, the final flush can fail too */
    if (rpm != NULL && fclose(rpm) != 0) {
        warn("fclose");
        rc = -1;
    }

    /* the package only takes its name once it is complete */
    if (tmpname != NULL && rc == 0 && rename(tmpname, filename) == -1) {
        warn("rename");
        rc = -1;
    }

    if (tmpname != NULL && rc != 0 && unlink(tmpname) == -1) {
        warn("unlink");
    }

    free(tmpname);

    /* back to the starting point and clean up */
    if (chdir(cwd) == -1) {
        warn("chdir");
    }

    return rc;
}
