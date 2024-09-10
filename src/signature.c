/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/header.h>
#include <json.h>

#include "tarpm.h"

/*
 * Extract the data of the RPM signature and convert it to JSON data.
 * Returns 0 on success, -1 on error.
 */
int
extract_signature(const int fd, const char *output_dir)
{
    struct rpmsighdr sig;
    struct rpmsighdridx sigidx;

    assert(fd > 0);
    assert(output_dir != NULL);

    /* zero out the structures */
    memset(&sig, 0, sizeof(sig));
    memset(&sigidx, 0, sizeof(sigidx));

    /* read in the signature */
    if (read(fd, &sig, sizeof(sig)) != sizeof(sig)) {
        err(EXIT_FAILURE, "read");
    }

    /* verify the magic and reserved values are correct */
    if (memcmp(sig.magic, "\216\255\350\001", 4)) {
        err(EXIT_FAILURE, "magic value mismatch, not an RPM");
    }

    if (memcmp(sig.reserved, "\0\0\0\0", 4)) {
        err(EXIT_FAILURE, "reserved value mismatch, not an RPM");
    }

    sig.nindex = ntohl(sig.nindex);
    sig.hsize = ntohl(sig.hsize);

printf("nindex=|%d|, hsize=|%d|\n", sig.nindex, sig.hsize);

    return 0;
}
