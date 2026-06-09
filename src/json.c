/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <json.h>

#include "tarpm.h"

/*
 * Wrapper for reading in a JSON file.
 */
struct json_object *
read_json_file(const char *input_file)
{
    struct json_object *obj = NULL;

    assert(input_file != NULL);

    if (access(input_file, R_OK) == -1) {
        warn(_("*** missing or unreadable %s"), input_file);
        return NULL;
    }

    obj = json_object_from_file(input_file);

    if (obj == NULL) {
        warnx(_("*** json_object_from_file: %s"), json_util_get_last_err());
        return NULL;
    }

    return obj;
}

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

    if (data == NULL || output_dir == NULL || output_file == NULL) {
        return -1;
    }

    /* write the JSON data for a file */
    s = joinpath(output_dir, output_file, NULL);
    assert(s != NULL);

    fp = fopen(s, "w");

    if (fp == NULL) {
        warn("fopen");
        return -1;
    }

    free(s);
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
    int r = 0;
    int len = 0;
    struct json_object_iter iter;

    if (data == NULL) {
        return;
    }

    /* clean up the JSON object memory usage */
    json_object_object_foreachC(data, iter) {
        if (json_object_get_type(iter.val) == json_type_array) {
            len = json_object_array_length(iter.val);

            for (r = 0; r < len; r++) {
                json_object_array_put_idx(iter.val, r, NULL);
            }
        }
    }

    json_object_put(data);
    return;
}
