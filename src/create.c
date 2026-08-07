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
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <json_object.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "tarpm.h"

static int trimlen = -1;
static struct archive *payload = NULL;

/* Helper for nftw() from create_payload() */
static int
add_payload_entry(const char *fpath, const struct stat *sb, __attribute__((unused)) int tflag, __attribute__((unused)) struct FTW *ftwbuf)
{
    int fd = -1;
    ssize_t len = 0;
    char buf[BUFSIZ];
    char *hardlink = NULL;
    bool is_hardlink = false;
    const char *vpath = NULL;
    char *full_path = NULL;
    struct archive_entry *entry = NULL;

    /*
     * skip the payload staging dir, but count its length as the path
     * trim length
     */
    if (trimlen == -1) {
        trimlen = strlen(fpath);
        return 0;
    } else {
        vpath = fpath + trimlen;

        while (*vpath == '/') {
            vpath++;
        }
    }

    entry = archive_entry_new();

    /* is this file a hardlink? */
    hardlink = lookup_inode(sb->st_ino);

    if (hardlink != NULL && strcmp(vpath, hardlink)) {
        is_hardlink = true;
    }

    if (S_ISREG(sb->st_mode)) {
        if (is_hardlink) {
            archive_entry_set_hardlink(entry, hardlink);
        } else {
            archive_entry_set_filetype(entry, AE_IFREG);
        }
    } else if (S_ISLNK(sb->st_mode) && (tflag == FTW_SL || tflag == FTW_SLN)) {
        archive_entry_set_filetype(entry, AE_IFLNK);
        memset(buf, '\0', sizeof(buf));

        if (readlink(fpath, buf, sizeof(buf) - 1) == -1) {
            warn("readlink");
            return -1;
        }

        archive_entry_set_symlink(entry, buf);
    } else if (S_ISSOCK(sb->st_mode)) {
        archive_entry_set_filetype(entry, AE_IFSOCK);
    } else if (S_ISCHR(sb->st_mode)) {
        archive_entry_set_filetype(entry, AE_IFCHR);
    } else if (S_ISBLK(sb->st_mode)) {
        archive_entry_set_filetype(entry, AE_IFBLK);
    } else if (S_ISDIR(sb->st_mode)) {
        archive_entry_set_filetype(entry, AE_IFDIR);
    } else if (S_ISFIFO(sb->st_mode)) {
        archive_entry_set_filetype(entry, AE_IFIFO);
    } else {
        warn("stat");
        return -1;
    }

    /* prepend ./ to pathname (cpio does this) */
    xasprintf(&full_path, "./%s", vpath);
    archive_entry_set_pathname(entry, full_path);
    free(full_path);

    /* XXX - get these from actual files on disk? */
    /* set timestamps */
    archive_entry_set_atime(entry, sb->st_atime, 0);
    archive_entry_set_mtime(entry, sb->st_mtime, 0);
    archive_entry_set_ctime(entry, sb->st_ctime, 0);

    /* inode number, device ID, hard link count */
    archive_entry_set_ino(entry, sb->st_ino);
    archive_entry_set_dev(entry, sb->st_dev);
    archive_entry_set_nlink(entry, sb->st_nlink);

    /* XXX - get this from %files/fallback on actual files/fallback on 0:0 ? */
    archive_entry_set_gid(entry, 0);
    archive_entry_set_uid(entry, 0);

    if (!is_hardlink) {
        archive_entry_set_size(entry, sb->st_size);
    }

    /* XXX - get this from %files ? */
    /* only set permission bits, not file type bits */
    archive_entry_set_perm(entry, sb->st_mode & ACCESSPERMS);

    /* write the entry header */
    archive_write_header(payload, entry);

    /* write the data to the payload for regular files only */
    if (S_ISREG(sb->st_mode) && !is_hardlink) {
        if ((fd = open(fpath, O_RDONLY)) == -1) {
            warn("open");
            return -1;
        } else {
            memset(buf, 0, sizeof(buf));
        }

        while ((len = read(fd, buf, sizeof(buf))) > 0) {
            archive_write_data(payload, buf, len);
        }

        if (len == -1) {
            warn("read");
            return -1;
        }

        if (close(fd) == -1) {
            warn("close");
            return -1;
        }
    }

    /* finalize the entry */
    if (archive_write_finish_entry(payload) != ARCHIVE_OK) {
        warnx("archive_write_finish_entry: %s", archive_error_string(payload));
        archive_entry_free(entry);
        return -1;
    }

    archive_entry_free(entry);
    return 0;
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

        free(trailer_data);
    }

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
 * Helper for create_rpm() that writes the payload data to a temporary
 * file.  Returns an open file descriptor that can then be used later when putting together the final RPM.
 * Caller must close the file descriptor when done which will remove the temporary file associated with it.
 *
 * Returns -1 on failure.
 */
static int
create_payload(struct json_object *header, const char *payload_subdir)
{
    int payloadfd = -1;
    char *template = NULL;
    const char *tag = NULL;
    char *opts = NULL;
    struct json_object *tags = NULL;

    if (header == NULL || payload_subdir == NULL) {
        return -1;
    }

    /* initialize inode list for hardlink handling */
    if (add_inodes(payload_subdir) != 0) {
        warnx("add_inodes");
        return -1;
    }

    /* get the header tags array */
    if (json_object_object_get_ex(header, "tags", &tags) == 0) {
        warnx(_("*** missing tags in header data"));
    }

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
    archive_write_set_format_cpio_newc(payload);

    /* get the compression algorithm type */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR));

    if (tag == NULL) {
        /* default to no compression */
        archive_write_add_filter_none(payload);
    } else {
        if (!strcmp(tag, "gzip")) {
            archive_write_add_filter_gzip(payload);
        } else if (!strcmp(tag, "bzip2")) {
            archive_write_add_filter_bzip2(payload);
        } else if (!strcmp(tag, "xz")) {
            archive_write_add_filter_xz(payload);
        } else if (!strcmp(tag, "lzma")) {
            archive_write_add_filter_lzma(payload);
        } else if (!strcmp(tag, "zstd")) {
            archive_write_add_filter_zstd(payload);
        } else {
            /* default to no compression */
            archive_write_add_filter_none(payload);
        }
    }

    /* set the compression level */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

    if (tag != NULL) {
        xasprintf(&opts, "compression-level=%s", tag);
        archive_write_set_options(payload, opts);
        free(opts);
    }

    /* open the payload for writing */
    if (archive_write_open_fd(payload, payloadfd) != ARCHIVE_OK) {
        errx(EXIT_FAILURE, "archive_write_open_FILE: %s", archive_error_string(payload));
    }

    /* reset the path trimlen */
    trimlen = -1;

    /* write the entries to the payload */
    if (nftw(payload_subdir, add_payload_entry, 25, FTW_MOUNT | FTW_PHYS) == -1) {
        warn("nftw");
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

    free_inodes();

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
    struct json_object *number = NULL;
    struct json_object *value = NULL;
    size_t j = 0;

    if (header == NULL || hdr == NULL || hdrinfo == NULL || payloadfd == -1) {
        return -1;
    }

    /* get the tags array from the header */
    if (json_object_object_get_ex(header, "tags", &tags) == 0) {
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

        if (json_object_object_get_ex(entry, "number", &number)) {
            if (json_object_get_int(number) == RPMTAG_PAYLOADSHA256ALT) {
                if (json_object_object_get_ex(entry, "value", &value)) {
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
    if (json_object_object_get_ex(signature, "tags", &tags) == 0) {
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

    if (payloadsize > 4294967296) {
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
    payloadfd = create_payload(header, PAYLOAD_SUBDIR);

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

    /* free the old header structures */
    free(hdr);
    free(hdrinfo->estart);
    free(hdrinfo->datastart);
    free(hdrinfo);

    /* regenerate the header with updated digests */
    if (create_header(header, &hdr, &hdrinfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to reconstruct RPM header"));
    }

    /* recalculate the digests and update the signature data using the updated header */
    if (update_signature(signature, header, hdr, hdrinfo, payloadfd) != 0) {
        close(payloadfd);
        errx(EXIT_FAILURE, "update_signature");
    }

    /* free the old signature structures */
    free(sig);
    free(siginfo->estart);
    free(siginfo->datastart);
    free(siginfo);

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
    free(sig);
    free(siginfo->estart);
    free(siginfo->datastart);
    free(siginfo);
    free(hdr);
    free(hdrinfo->estart);
    free(hdrinfo->datastart);
    free(hdrinfo);
    free(rawlead);

    return;
}
