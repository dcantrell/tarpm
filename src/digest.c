/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <err.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/evp.h>

#include "tarpm.h"

/*
 * Compute digests for the signature header, which are of the header
 * header plus the package payload.  The signature header records the
 * MD5 digest, SHA-1 digest, and SHA-256.  This is a function called
 * multiple times depending on the algorithm needed.
 *
 * This is not a general purpose digest computation function.  It's
 * specifically for feeding in two parts of an RPM in to the algorithm
 * to compute a digest.
 *
 * Returns an allocated computed digest or NULL on failure.  Caller
 * must free the returned buffer.
 */
unsigned char *
compute_signature_digest(const int type, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct json_object *data, const int fd)
{
    unsigned char *r = NULL;
    int i = -1;
    int len = 0;
    uint32_t nentries = 0;
    uint32_t nbytes = 0;
    uint32_t n = 0;
    uint8_t *trailer_data = NULL;
    size_t trailer_size = 0;
    unsigned int digest_sz = 0;
    unsigned char buf[BUFSIZ];
    unsigned char digest[EVP_MAX_MD_SIZE];
    EVP_MD_CTX *ctx = NULL;

    /* parameter validation */
    if (type <= 0) {
        warnx("*** invalid digest type: %d", type);
        return NULL;
    }

    if (hdr == NULL || hdrinfo == NULL || data == NULL) {
        warnx("*** missing header structs");
        return NULL;
    }

    if (fd == -1) {
        warnx("*** invalid file descriptor");
        return NULL;
    }

    /* create a new context */
    ctx = EVP_MD_CTX_new();

    if (ctx == NULL) {
        warn("EVP_MD_CTX_new");
        return NULL;
    }

    /* initialize with the specified digest type */
    if (type == TARPM_DIGEST_MD5) {
        i = EVP_DigestInit(ctx, EVP_md5());
    } else if (type == TARPM_DIGEST_SHA1) {
        i = EVP_DigestInit(ctx, EVP_sha1());
    } else if (type == TARPM_DIGEST_SHA256) {
        i = EVP_DigestInit(ctx, EVP_sha256());
    } else {
        warnx("*** unsupported digest type: %d", type);
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    if (i == 0) {
        warn("EVP_DigestInit");
        return NULL;
    }

    /* add the header data */
    nentries = ntohl(hdr->nentries);

    if (EVP_DigestUpdate(ctx, hdrinfo->estart, sizeof(struct rpmhdrentry) * nentries) == 0) {
        warn("EVP_DigestUpdate");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    nbytes = ntohl(hdr->nbytes);
    n = nbytes;
    i = -1;

    if (has_trailer(nentries, hdrinfo->estart)) {
        n -= 16;
        i = get_trailer_data(data, &trailer_data, &trailer_size);
    }

    if (EVP_DigestUpdate(ctx, hdrinfo->datastart, n) == 0) {
        warn("EVP_DigestUpdate");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    if (i == 0 && trailer_size == 16) {
        if (EVP_DigestUpdate(ctx, trailer_data, trailer_size) == 0) {
            warn("EVP_DigestUpdate");
            EVP_MD_CTX_free(ctx);
            return NULL;
        }

        free(trailer_data);
    }

    /* add the payload data */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        warn("lseek");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    len = read(fd, buf, sizeof(buf));

    if (len == -1) {
        warn("read");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    while (len > 0) {
        if (EVP_DigestUpdate(ctx, buf, len) == 0) {
            warn("EVP_DigestUpdate");
            EVP_MD_CTX_free(ctx);
            return NULL;
        }

        len = read(fd, buf, sizeof(buf));

        if (len == -1) {
            warn("read");
            EVP_MD_CTX_free(ctx);
            return NULL;
        }
    }

    /* finalize the digest context */
    if (EVP_DigestFinal(ctx, digest, &digest_sz) == 0) {
        warn("EVP_DigestFinal");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    r = xalloc(digest_sz);
    memcpy(r, digest, digest_sz);

    return r;
}
