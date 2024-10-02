/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <assert.h>
#include <err.h>
#include <json.h>

#include "tarpm.h"

/*
 * Takes the JSON data and writes it to the output_file in output_dir.
 * Returns 0 on success, -1 on error.
 */
int
write_json_file(struct json_object *data, const char *output_dir, const char *output_file)
{
    char *s = NULL;
    const char *js = NULL;
    FILE *fp = NULL;
    int r = 0;
    int flags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;

    assert(data != NULL);
    assert(output_dir != NULL);
    assert(output_file != NULL);

    /* write the JSON data for a file */
    s = joinpath(output_dir, output_file, NULL);
    assert(s != NULL);

    fp = fopen(s, "w");

    if (fp == NULL) {
        warn("fopen");
        return -1;
    }

    js = json_object_to_json_string_ext(data, flags);

    if (js == NULL) {
        errx(EXIT_FAILURE, "unable to turn JSON object in to string");
    }

    fprintf(fp, "%s\n", js);
    r = fflush(fp);

    if (r != 0) {
        warn("fflush");
    }

    r = fclose(fp);

    if (r != 0) {
        warn("fclose");
    }

    return 0;
}

/*
 * Free memory used by JSON object
 */
void
free_json(struct json_object *data)
{
    struct json_object_iter iter;

    if (data == NULL) {
        return;
    }

    /* clean up the JSON object memory usage */
    json_object_object_foreachC(data, iter) {
        json_object_put(iter.val);
    }

    data = NULL;

    return;
}
