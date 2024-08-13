/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <rpm/header.h>

#include "tarpm.h"

/*
 * Extract the data of the RPM lead and convert it to JSON data.
 * Returns 0 on success, -1 on error.
 */
int
extract_lead(const char *rpm, const char *output_dir)
{
    struct rpmlead lead;

    assert(rpm != NULL);
    assert(output_dir != NULL);

    /* zero out the lead structure */
    memset(&lead, 0, sizeof(lead));












    return 0;
}

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
