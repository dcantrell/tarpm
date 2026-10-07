/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <unistd.h>
#include <err.h>
#include <string.h>
#include <arpa/inet.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>

#include "tarpm.h"

/*
 * Give back the OpenSSL algorithm for one of our digest types, NULL
 * for a type we do not know.
 */
static const EVP_MD *
digest_algorithm(const int type)
{
    if (type == TARPM_DIGEST_MD5) {
        return EVP_md5();
    } else if (type == TARPM_DIGEST_SHA1) {
        return EVP_sha1();
    } else if (type == TARPM_DIGEST_SHA256) {
        return EVP_sha256();
    } else if (type == TARPM_DIGEST_SHA3_256) {
        return EVP_sha3_256();
    } else if (type == TARPM_DIGEST_SHA512) {
        return EVP_sha512();
    }

    return NULL;
}

/*
 * Render a raw digest as the lowercase hex string rpm records in the
 * header.  Caller must free the returned string.
 */
static char *
hexdigest(const unsigned char *digest, const unsigned int len)
{
    unsigned int i = 0;
    char *r = NULL;

    r = xcalloc((len * 2) + 1, sizeof(char));

    for (i = 0; i < len; i++) {
        sprintf(&r[i * 2], "%02x", (unsigned int) digest[i]);
    }

    return r;
}

/*
 * Give back a digest of all zeroes in the hex form the header records.
 * rpm sizes its header with one of these in nullDigest() so the two
 * sizing passes agree, and we do the same.  Returns an allocated
 * string or NULL for a type we do not know.  Caller must free it.
 */
char *
nul_digest(const int type)
{
    int len = 0;
    const EVP_MD *md = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];

    md = digest_algorithm(type);

    if (md == NULL) {
        warnx(_("*** unsupported digest type: %d"), type);
        return NULL;
    }

    /*
     * Use EVP_MD_size() to maintain compatibility going back to
     * OpenSSL 1.1.x
     */
    len = EVP_MD_size(md);

    if (len <= 0) {
        warnx("EVP_MD_size");
        return NULL;
    }

    memset(digest, 0, sizeof(digest));

    return hexdigest(digest, (unsigned int) len);
}

/*
 * Digest everything the file descriptor holds, which is the payload as
 * it lands in the package.  The size the payload takes up goes in size
 * if the caller asked for it.  Returns an allocated hex string or NULL
 * on failure.  Caller must free the string.
 */
char *
payload_digest(const int type, const int payloadfd, uint64_t *size)
{
    char *r = NULL;
    ssize_t len = 0;
    uint64_t total = 0;
    unsigned int digestlen = 0;
    unsigned char buf[BUFSIZ];
    unsigned char digest[EVP_MAX_MD_SIZE];
    EVP_MD_CTX *ctx = NULL;

    if (payloadfd == -1) {
        return NULL;
    }

    if (lseek(payloadfd, 0, SEEK_SET) == -1) {
        warn("lseek");
        return NULL;
    }

    ctx = EVP_MD_CTX_new();

    if (ctx == NULL || EVP_DigestInit(ctx, digest_algorithm(type)) == 0) {
        warn("EVP_DigestInit");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    while ((len = read(payloadfd, buf, sizeof(buf))) > 0) {
        if (EVP_DigestUpdate(ctx, buf, len) == 0) {
            warn("EVP_DigestUpdate");
            EVP_MD_CTX_free(ctx);
            return NULL;
        }

        total += len;
    }

    if (len == -1) {
        warn("read");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digestlen) == 0) {
        warn("EVP_DigestFinal_ex");
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    r = hexdigest(digest, digestlen);

    if (size != NULL) {
        *size = total;
    }

    EVP_MD_CTX_free(ctx);

    return r;
}

/*
 * Digest the payload with the compression taken back off, which is
 * what the ALT tags in the main header record.  The uncompressed size
 * goes in size if the caller asked for it.  Returns an allocated hex
 * string or NULL on failure.  Caller must free the string.
 */
char *
archive_digest(const int type, const int payloadfd, uint64_t *size)
{
    char *r = NULL;
    ssize_t len = 0;
    uint64_t total = 0;
    unsigned int digestlen = 0;
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

    if (ctx == NULL || EVP_DigestInit(ctx, digest_algorithm(type)) == 0) {
        warn("EVP_DigestInit");
        goto cleanup_archive_digest;
    }

    if (archive_read_next_header(raw, &entry) != ARCHIVE_OK) {
        warnx("archive_read_next_header: %s", archive_error_string(raw));
        goto cleanup_archive_digest;
    }

    while ((len = archive_read_data(raw, buf, sizeof(buf))) > 0) {
        if (EVP_DigestUpdate(ctx, buf, len) == 0) {
            warn("EVP_DigestUpdate");
            goto cleanup_archive_digest;
        }

        total += len;
    }

    if (len < 0) {
        warnx("archive_read_data: %s", archive_error_string(raw));
        goto cleanup_archive_digest;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digestlen) == 0) {
        warn("EVP_DigestFinal_ex");
        goto cleanup_archive_digest;
    }

    r = hexdigest(digest, digestlen);

    if (size != NULL) {
        *size = total;
    }

cleanup_archive_digest:
    EVP_MD_CTX_free(ctx);
    archive_read_free(raw);

    return r;
}

/*
 * Compute a digest over the header, and over the payload as well for
 * the MD5 the signature header records.  rpm only feeds the payload
 * in to MD5, every other signature digest covers the header alone.
 *
 * This is not a general purpose digest function.  It only feeds those
 * two parts of an RPM in to the algorithm.
 *
 * Returns an allocated digest or NULL on failure.  Caller must free
 * the buffer.
 */
unsigned char *
mksigdigest(const int type, const struct rpmhdr *hdr, const struct rpmhdrinfo *hdrinfo, const struct json_object *data, const int fd)
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
    if (digest_algorithm(type) == NULL) {
        warnx("*** unsupported digest type: %d", type);
        goto cleanup_mksigdigest;
    }

    if (EVP_DigestInit(ctx, digest_algorithm(type)) == 0) {
        warn("EVP_DigestInit");
        goto cleanup_mksigdigest;
    }

    /* add the header magic (same as rpm's rpm_header_magic) */
    if (EVP_DigestUpdate(ctx, hdr, 8) == 0) {
        warn("EVP_DigestUpdate");
        goto cleanup_mksigdigest;
    }

    if (EVP_DigestUpdate(ctx, &(hdr->nentries), sizeof(hdr->nentries)) == 0 || EVP_DigestUpdate(ctx, &(hdr->nbytes), sizeof(hdr->nbytes)) == 0) {
        warn("EVP_DigestUpdate");
        goto cleanup_mksigdigest;
    }

    nentries = ntohl(hdr->nentries);
    nbytes = ntohl(hdr->nbytes);

    /* add the header entries */
    if (EVP_DigestUpdate(ctx, hdrinfo->estart, sizeof(struct rpmhdrentry) * nentries) == 0) {
        warn("EVP_DigestUpdate");
        goto cleanup_mksigdigest;
    }

    n = nbytes;
    i = -1;

    if (has_trailer(nentries, hdrinfo->estart)) {
        n -= RPM_TRAILER_SIZE;
        i = get_trailer_data(data, &trailer_data, &trailer_size);

        /* digest the trailer we are going to write, not the one we read */
        if (i == 0) {
            fix_trailer_offset(trailer_data, trailer_size, nentries);
        }
    }

    if (EVP_DigestUpdate(ctx, hdrinfo->datastart, n) == 0) {
        warn("EVP_DigestUpdate");
        goto cleanup_mksigdigest;
    }

    if (i == 0 && trailer_size == RPM_TRAILER_SIZE) {
        if (EVP_DigestUpdate(ctx, trailer_data, trailer_size) == 0) {
            warn("EVP_DigestUpdate");
            goto cleanup_mksigdigest;
        }
    }

    /* add the payload data, which only the MD5 digest covers */
    if (type == TARPM_DIGEST_MD5) {
        if (lseek(fd, 0, SEEK_SET) == -1) {
            warn("lseek");
            goto cleanup_mksigdigest;
        }

        len = read(fd, buf, sizeof(buf));

        if (len == -1) {
            warn("read");
            goto cleanup_mksigdigest;
        }

        while (len > 0) {
            if (EVP_DigestUpdate(ctx, buf, len) == 0) {
                warn("EVP_DigestUpdate");
                goto cleanup_mksigdigest;
            }

            len = read(fd, buf, sizeof(buf));

            if (len == -1) {
                warn("read");
                goto cleanup_mksigdigest;
            }
        }
    }

    /* finalize the digest context */
    if (EVP_DigestFinal(ctx, digest, &digest_sz) == 0) {
        warn("EVP_DigestFinal");
        goto cleanup_mksigdigest;
    }

    r = xalloc(digest_sz);
    memcpy(r, digest, digest_sz);

cleanup_mksigdigest:
    free(trailer_data);
    EVP_MD_CTX_free(ctx);
    return r;
}
