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
#include <assert.h>
#include <err.h>
#include <arpa/inet.h>
#include <json_object.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>
#include <archive.h>
#include <archive_entry.h>

#include "tarpm.h"

static int trimlen = -1;
static struct archive *payload = NULL;

/* Helper for nftw() from write_payload() */
static int
add_payload_entry(const char *fpath, const struct stat *sb, __attribute__((unused)) int tflag, __attribute__((unused)) struct FTW *ftwbuf)
{
    int fd = -1;
    ssize_t len = 0;
    char buf[BUFSIZ];
    char *hardlink = NULL;
    bool is_hardlink = false;
    const char *vpath = NULL;
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

    archive_entry_set_pathname(entry, vpath);
    archive_entry_set_atime(entry, sb->st_atime, 0);
    archive_entry_set_mtime(entry, sb->st_mtime, 0);
    archive_entry_set_ctime(entry, sb->st_ctime, 0);
    archive_entry_set_perm(entry, sb->st_mode);

    /* XXX - get this from %files ? */
    archive_entry_set_gid(entry, 0);
    archive_entry_set_uid(entry, 0);

    if (!is_hardlink) {
        archive_entry_set_size(entry, sb->st_size);
    }

    /* XXX - get this from %files ? */
    archive_entry_set_perm(entry, sb->st_mode);

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

        if ((len = read(fd, buf, sizeof(buf))) == -1) {
            warn("read");
            return -1;
        }

        while (len > 0) {
            archive_write_data(payload, buf, len);

            if ((len = read(fd, buf, sizeof(buf))) == -1) {
                warn("read");
                return -1;
            }
        }

        if (close(fd) == -1) {
            warn("close");
            return -1;
        }
    }

    archive_entry_free(entry);
    return 0;
}

/* Helper for create_rpm() that writes header data to the RPM */
static void
write_header(FILE *rpm, struct rpmhdr *hdr, struct rpmhdrinfo *hdrinfo, bool is_signature, struct json_object *data)
{
    uint32_t i = 0;
    uint32_t n = 0;
    uint32_t nentries = 0;
    uint32_t nbytes = 0;
    uint32_t hlen = 0;
    uint32_t padlen = 0;
    uint8_t padding[8] = {0};
    struct json_object *tags = NULL;
    struct json_object *entry = NULL;
    struct json_object *trailer_obj = NULL;
    struct json_object *value_obj = NULL;
    const char *trailer_value = NULL;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    int r = 0;
    bool has_trailer = false;

    assert(rpm != NULL);
    assert(hdr != NULL);
    assert(hdrinfo != NULL);

    if (fwrite(hdr, sizeof(*hdr), 1, rpm) != 1) {
        warn("fwrite");
    }

    /* write the signature index entries */
    nentries = ntohl(hdr->nentries);

    if (fwrite(hdrinfo->estart, sizeof(struct rpmhdrentry), nentries, rpm) != nentries) {
        warn("fwrite");
    }

    /* write the signature data */
    nbytes = ntohl(hdr->nbytes);
    n = nbytes;

    /*
     * in HEADER_SIGNATURES or HEADER_IMMUTABLE, actual data is 16
     * bytes less, because the trailer is counted in nbytes but
     * not written in the main data section
     */
    for (i = 0; i < nentries; i++) {
        if (ntohl(hdrinfo->estart[i].tag) == HEADER_SIGNATURES || ntohl(hdrinfo->estart[i].tag) == HEADER_IMMUTABLE) {
            /* trailer size */
            n -= 16;
            has_trailer = true;
            break;
        }
    }

    if (fwrite(hdrinfo->datastart, 1, n, rpm) != n) {
        warn("fwrite");
    }

    /* write the trailer if present (for both signature and header sections) */
    if (has_trailer && data != NULL) {
        if (json_object_object_get_ex(data, "tags", &tags) == 1) {
            for (i = 0; i < json_object_array_length(tags); i++) {
                entry = json_object_array_get_idx(tags, i);

                if (json_object_object_get_ex(entry, "trailer", &trailer_obj) == 1) {
                    if (json_object_object_get_ex(entry, "value", &value_obj) == 1) {
                        trailer_value = json_object_get_string(value_obj);
                        r = rpmBase64Decode(trailer_value, (void **) &trailer_data, &trailer_size);

                        if (r == 0 && trailer_size == 16) {
                            if (fwrite(trailer_data, 1, trailer_size, rpm) != trailer_size) {
                                warn("fwrite");
                            }
                        }

                        free(trailer_data);
                    }

                    break;
                }
            }
        }
    }

    /* write padding after signature data to align to 8-byte boundary */
    if (is_signature) {
        /* padding is based on index entries + full nbytes (including trailer) */
        hlen = nentries * sizeof(struct rpmhdrentry) + nbytes;
        padlen = (8 - (hlen % 8)) % 8;

        if (padlen > 0) {
            if (fwrite(padding, 1, padlen, rpm) != padlen) {
                warn("fwrite");
            }
        }
    }

    return;
}

/* Helper for create_rpm() that writes the payload data to the RPM */
static void
write_payload(FILE *rpm, struct json_object *header, const char *payload_subdir)
{
    const char *tag = NULL;
    char *opts = NULL;
    struct json_object *tags = NULL;

    assert(rpm != NULL);
    assert(header != NULL);

    /* initialize inode list for hardlink handling */
    if (add_inodes(payload_subdir) != 0) {
        warnx("add_inodes");
    }

    /* create a new payload writer */
    payload = archive_write_new();

    /* set the payload format, which is always the same */
    archive_write_set_format_cpio_newc(payload);

    /* get the header tags array */
    if (json_object_object_get_ex(header, "tags", &tags) == 0) {
        warnx(_("*** missing tags in header data"));
    }

    /* get the compression algorithm type */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR));

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

    /* set the compression level */
    tag = get_tag_value(tags, rpmTagGetName(RPMTAG_PAYLOADFLAGS));

    if (tag != NULL) {
        xasprintf(&opts, "compression-level=%s", tag);
        archive_write_set_options(payload, opts);
        free(opts);
    }

    /* open the payload for writing */
    if (archive_write_open_FILE(payload, rpm) != ARCHIVE_OK) {
        errx(EXIT_FAILURE, "archive_write_open_FILE: %s", archive_error_string(payload));
    }

    /* write the entries to the payload */
printf("payload_subdir=|%s|\n", payload_subdir);
    if (nftw(payload_subdir, add_payload_entry, 25, FTW_MOUNT | FTW_PHYS) == -1) {
        warn("nftw");
    }

    /* close the payload */
#if ARCHIVE_VERSION_NUMBER < 3000000
    archive_write_close(payload);
    archive_write_finish(payload);
#else
    archive_write_free(payload);
#endif

    free_inodes();

    return;
}

/* Handler for -c mode (create) */
void
create_rpm(const char *filename, const char *cwd, const char *input_dir)
{
    FILE *rpm = NULL;
    struct stat sb;
    struct json_object *signature = NULL;
    struct json_object *header = NULL;
    struct rpmlead *rawlead = NULL;
    struct rpmhdr *sig = NULL;
    struct rpmhdrinfo *siginfo = NULL;
    struct rpmhdr *hdr = NULL;
    struct rpmhdrinfo *hdrinfo = NULL;

    assert(filename != NULL);
    assert(cwd != NULL);
    assert(input_dir != NULL);

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

    /* create the signature */
    if (create_header(signature, &sig, &siginfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM signature"));
    }

    /* create the header (the main header) */
    if (create_header(header, &hdr, &hdrinfo) == -1) {
        errx(EXIT_FAILURE, _("*** unable to construct RPM header"));
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
    write_header(rpm, sig, siginfo, true, signature);

    /* write the header to the RPM */
    write_header(rpm, hdr, hdrinfo, false, header);

    /* write the payload to the RPM */
    write_payload(rpm, header, PAYLOAD_SUBDIR);

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
