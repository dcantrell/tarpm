/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <arpa/inet.h>
#include "tarpm.h"

/*
 * Generate a "signature" or "header" JSON structure for output.
 */
struct json_object *
generate_json(const struct rpmsignature *sig, const struct rpmsigvalues *svals)
{
    struct json_object *out = NULL;
    char *s = NULL;

    assert(sig != NULL);
    assert(svals != NULL);

    out = json_object_new_object();

    xasprintf(&s, "0x%X", sig->magic);
    json_object_object_add(out, RPM_SIGNATURE_MAGIC_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%04d", sig->reserved);
    json_object_object_add(out, RPM_SIGNATURE_RESERVED_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig->nentries);
    json_object_object_add(out, RPM_SIGNATURE_NENTRIES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", svals->ilen);
    json_object_object_add(out, RPM_SIGNATURE_ILEN_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", sig->nbytes);
    json_object_object_add(out, RPM_SIGNATURE_NBYTES_DESC, json_object_new_string(s));
    free(s);

    xasprintf(&s, "%d", svals->hlen);
    json_object_object_add(out, RPM_SIGNATURE_HLEN_DESC, json_object_new_string(s));
    free(s);

    return out;
}

/*
 * Generate a "signature" or "header" JSON array of entries for output.
 */
struct json_object *
generate_json_entries(const struct rpmsignature *sig, const struct rpmsigvalues *svals, struct rpmidxentry *entry, const bool signature)
{
    uint32_t i = 0;
    rpmSigTag tag = 0;
    uint32_t offset = 0;
    rpmTagType datatype = 0;
    uint32_t count = 0;
    struct json_object *jvals = NULL;
    struct json_object *arrayentry = NULL;
    char *s = NULL;

    assert(sig != NULL);
    assert(svals != NULL);
    assert(entry != NULL);

    jvals = json_object_new_array();

    for (i = 0; i < sig->nentries; i++) {
        tag = ntohl(entry[i].tag);
        offset = ntohl(entry[i].offset);
        datatype = ntohl(entry[i].type);
        count = ntohl(entry[i].count);
        arrayentry = json_object_new_object();

        if (signature) {
            xasprintf(&s, "%s", signature_tag_name(tag));
        } else {
            xasprintf(&s, "%s", rpmTagGetName(tag));
        }

        json_object_object_add(arrayentry, RPM_ENTRY_NAME_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", tag);
        json_object_object_add(arrayentry, RPM_ENTRY_TAG_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%s", tag_type(datatype));
        json_object_object_add(arrayentry, RPM_ENTRY_TYPE_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "0x%X", offset);
        json_object_object_add(arrayentry, RPM_ENTRY_OFFSET_DESC, json_object_new_string(s));
        free(s);

        xasprintf(&s, "%d", count);
        json_object_object_add(arrayentry, RPM_ENTRY_COUNT_DESC, json_object_new_string(s));
        free(s);

        add_entry_value(arrayentry, svals->datastart, offset, datatype, count);
        json_object_array_add(jvals, arrayentry);
    }

    return jvals;
}
