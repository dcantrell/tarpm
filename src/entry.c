/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include <rpm/rpmbase64.h>
#include <json.h>

#include "tarpm.h"

void
add_entry_value(struct json_object *arrayentry, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count)
{
    uint8_t *data = NULL;
    union datatypes dt;
    void *blob = NULL;
    char *s = NULL;

    assert(arrayentry != NULL);
    assert(buffer != NULL);

    /* move to the position of this entry's data */
    data = buffer + offset;

    /* read the value */
    switch (datatype) {
        case RPM_NULL_TYPE:
            s = strdup("(null)");
            break;
        case RPM_CHAR_TYPE:
            memcpy(&dt.c, data, sizeof(dt.c));
            xasprintf(&s, "%c", dt.c);
            break;
        case RPM_INT8_TYPE:
            memcpy(&dt.i8, data, sizeof(dt.i8));
            xasprintf(&s, "%d", dt.i8);
            break;
        case RPM_INT16_TYPE:
            memcpy(&dt.i16, data, sizeof(dt.i16));
            dt.i16 = ntohl(dt.i16);
            xasprintf(&s, "%d", dt.i16);
            break;
        case RPM_INT32_TYPE:
            memcpy(&dt.i32, data, sizeof(dt.i32));
            dt.i32 = ntohl(dt.i32);
            xasprintf(&s, "%d", dt.i32);
            break;
        case RPM_INT64_TYPE:
            memcpy(&dt.i64, data, sizeof(dt.i64));
            dt.i64 = ntohl(dt.i64);
            xasprintf(&s, "%ld", dt.i64);
            break;
        case RPM_STRING_TYPE:
            s = strdup((char *) data);
            break;
        case RPM_BIN_TYPE:
            blob = xalloc(count);
            assert(blob != NULL);
            memcpy(blob, data, count);
            s = rpmBase64Encode(blob, count, -1);
            free(blob);

            if (s == NULL) {
                err(EXIT_FAILURE, "rpmBase64Encode");
            }

            break;
        case RPM_STRING_ARRAY_TYPE:
            /* XXX: return "argv"; */
            break;
        case RPM_I18NSTRING_TYPE:
            s = strdup((char *) data);
            break;
        default:
            s = strdup("(unknown)");
            break;
    }

    /* add the value */
    if (s == NULL) {
        s = strdup("");
        assert(s != NULL);
    }

    json_object_object_add(arrayentry, RPM_SIGNATURE_VALUE_DESC, json_object_new_string(s));
    free(s);

    return;
}
