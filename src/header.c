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
 * Iterate over the RPM header and write the data to a JSON file in
 * output_dir.  Returns 0 on success, -1 on error.
 */
int
extract_header(Header h, const char *output_dir)
{
    rpmtd td = NULL;
    HeaderIterator hi = NULL;
    rpmTagType t = 0;

    assert(h != NULL);
    assert(output_dir != NULL);

    /* create a new tag data container */
    td = rpmtdNew();

    /* create a header iteator to walk the tags */
    hi = headerInitIterator(h);

    /* iterate over the header tags and add them to a JSON structure */
    while (headerNext(hi, td) == 1) {
        t = rpmtdType(td);

printf("tag=|%s (%d)|\n", rpmTagGetName(t), t);

        rpmtdReset(td);
    }

    /* clean up */
    headerFreeIterator(hi);
    rpmtdFreeData(td);
    rpmtdFree(td);

    return 0;
}
