/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <err.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <rpm/rpmbase64.h>
#include <json.h>

#include "tarpm.h"

void
add_entry_value(struct json_object *arrayentry, uint8_t *buffer, uint32_t offset, rpmTagType datatype, uint32_t count)
{
    uint32_t i = 0;
    uint8_t *data = NULL;
    union datatypes dt;
    void *blob = NULL;
    char *s = NULL;
    uint8_t *p = NULL;
    int c = -1;
    struct json_object *sa = NULL;

    if (arrayentry == NULL || buffer == NULL) {
        return;
    }

    /* move to the position of this entry's data */
    data = buffer + offset;

    /* read the value */
    switch (datatype) {
        case RPM_NULL_TYPE:
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string("(null)"));
            break;
        case RPM_CHAR_TYPE:
            memcpy(&dt.c, data, sizeof(dt.c));
            xasprintf(&s, "%c", dt.c);
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
            free(s);
            break;
        case RPM_INT8_TYPE:
            if (count == 1) {
                memcpy(&dt.i8, data, sizeof(dt.i8));
                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i8));
            } else {
                sa = json_object_new_array();
                p = data;

                for (i = 0; i < count; i++) {
                    memcpy(&dt.i8, p, sizeof(dt.i8));
                    json_object_array_add(sa, json_object_new_int(dt.i8));
                    p += sizeof(dt.i8);
                }

                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
            }

            break;
        case RPM_INT16_TYPE:
            if (count == 1) {
                memcpy(&dt.i16, data, sizeof(dt.i16));
                dt.i16 = ntohs(dt.i16);
                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i16));
            } else {
                sa = json_object_new_array();
                p = data;

                for (i = 0; i < count; i++) {
                    memcpy(&dt.i16, p, sizeof(dt.i16));
                    dt.i16 = ntohs(dt.i16);
                    json_object_array_add(sa, json_object_new_int(dt.i16));
                    p += sizeof(dt.i16);
                }

                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
            }

            break;
        case RPM_INT32_TYPE:
            if (count == 1) {
                memcpy(&dt.i32, data, sizeof(dt.i32));
                dt.i32 = ntohl(dt.i32);
                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int(dt.i32));
            } else {
                sa = json_object_new_array();
                p = data;

                for (i = 0; i < count; i++) {
                    memcpy(&dt.i32, p, sizeof(dt.i32));
                    dt.i32 = ntohl(dt.i32);
                    json_object_array_add(sa, json_object_new_int(dt.i32));
                    p += sizeof(dt.i32);
                }

                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
            }

            break;
        case RPM_INT64_TYPE:
            if (count == 1) {
                memcpy(&dt.i64, data, sizeof(dt.i64));
                dt.i64 = be64toh(dt.i64);
                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_int64(dt.i64));
            } else {
                sa = json_object_new_array();
                p = data;

                for (i = 0; i < count; i++) {
                    memcpy(&dt.i64, p, sizeof(dt.i64));
                    dt.i64 = be64toh(dt.i64);
                    json_object_array_add(sa, json_object_new_int64(dt.i64));
                    p += sizeof(dt.i64);
                }

                json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);
            }

            break;
        case RPM_STRING_TYPE:
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string((char *) data));
            break;
        case RPM_BIN_TYPE:
            blob = xalloc(count);
            memcpy(blob, data, count);
            s = rpmBase64Encode(blob, count, -1);
            free(blob);

            if (s == NULL) {
                err(EXIT_FAILURE, "rpmBase64Encode");
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string(s));
            free(s);

            break;
        case RPM_STRING_ARRAY_TYPE:
            sa = json_object_new_array();
            p = data;

            for (i = 0; i < count; i++) {
                c = asprintf(&s, "%s", (char *) p);

                if (c == -1) {
                    err(EXIT_FAILURE, "asprintf");
                }

                json_object_array_add(sa, json_object_new_string((char *) p));
                p += c + 1;

                free(s);
                s = NULL;
            }

            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, sa);

            break;
        case RPM_I18NSTRING_TYPE:
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string((char *) data));
            break;
        default:
            json_object_object_add(arrayentry, RPM_ENTRY_VALUE_DESC, json_object_new_string("(unknown)"));
            break;
    }

    return;
}
