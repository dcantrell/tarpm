/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <err.h>
#include <arpa/inet.h>
#include <json_object.h>
#include <rpm/header.h>
#include <rpm/rpmbase64.h>

#include "tarpm.h"

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
//static void
//write_payload(FILE *rpm, struct json_object *header, const char *payload_subdir)
//{
//    const char *tag = NULL;
//    char *opts = NULL;
//    struct archive *payload = NULL;
//
//    assert(rpm != NULL);
//    assert(header != NULL);
//
//    /* create a new payload writer */
//    output = archive_write_new();
//
//    /* get the compression algorithm type */
//    tag = get_tag_value(header, rpmTagGetName(RPMTAG_PAYLOADCOMPRESSOR));
//
//    if (!strcmp(tag, "gzip")) {
//        archive_write_add_filter_gzip(output);
//    } else if (!strcmp(tag, "bzip2")) {
//        archive_write_add_filter_bzip2(output);
//    } else if (!strcmp(tag, "xz")) {
//        archive_write_add_filter_xz(output);
//    } else if (!strcmp(tag, "lzma")) {
//        archive_write_add_filter_lzma(output);
//    } else if (!strcmp(tag, "zstd")) {
//        archive_write_add_filter_zstd(output);
//    } else {
//        /* default to no compression */
//        archive_write_add_filter_none(output);
//    }
//
//    /* set the compression level */
//    tag = get_tag_value(header, rpmTagGetName(RPMTAG_PAYLOADFLAGS));
//
//    if (tag != NULL) {
//        xasprintf(&opts, "compression-level=%s", tag);
//        archive_write_set_options(output, opts);
//        free(opts);
//    }
//
//
//
//
//
//
///*
//
//4) Create a new libarchive object from the rpm fd
//5) Use nftw() to loop over payload_subdir and add each entry to the libarchive object
//6) Close the libarchive object
//7) return
//
//Errors here should be reported with warn() or err() and either return early or exit
//
//*/
//
//    return;
//}

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
//    write_payload(rpm, header, PAYLOAD_SUBDIR);

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
