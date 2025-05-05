/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "ccan/str/str.h"

#include "jjc.h"
#include "count.h"

static int jjc_read_level (json_t *o, int level, struct jjc_counts *jj, int nodefactor);

static int jjc_read_vertex (json_t *o, int level, struct jjc_counts *jj, int nodefactor)
{
    struct count *count;
    const char *type = NULL;
    json_t *count_json;
    json_t *with = NULL;
    json_error_t error;
    int exclusive = 0;

    if (json_unpack_ex (o, &error, 0, "{ s:s s:o s?b s?o }",
                       "type", &type,
                       "count", &count_json,
                       "exclusive", &exclusive,
                       "with", &with) < 0) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "level %d: %s", level, error.text);
        errno = EINVAL;
        return -1;
    }
    if (!(count = count_create (count_json, &error))) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "level %d: %s", level, error.text);
        errno = EINVAL;
        return -1;
    }
    if (streq (type, "node")) {
        if (nodefactor < 0) {
            sprintf (jj->error, "Non-integer count not allowed above node");
            errno = EINVAL;
            return -1;
        }
        jj->nnodes = count;
        jj->nodefactor = nodefactor;
        if (exclusive)
            jj->exclusive = true;
    } else {
        // Set negative as flag to indicate non-integer count found
        nodefactor = count->integer ? nodefactor * count_first (count) : -1;
    }
    if (streq (type, "slot"))
        jj->nslots = count;
    else if (streq (type, "core"))
        jj->slot_size = count;
    else if (streq (type, "gpu"))
        jj->slot_gpus = count;
    // ignore unknown resources
    if (with)
        return jjc_read_level (with, level+1, jj, nodefactor);
    return 0;

}

static int jjc_read_level (json_t *o, int level, struct jjc_counts *jj, int nodefactor)
{
    int i;
    json_t *v = NULL;

    if (!json_is_array (o)) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "level %d: must be an array", level);
        errno = EINVAL;
        return -1;
    }
    json_array_foreach (o, i, v) {
        if (jjc_read_vertex (v, level, jj, nodefactor) < 0)
            return -1;
    }
    return 0;
}

int jjc_get_counts (const char *spec, struct jjc_counts *jj)
{
    json_t *o = NULL;
    json_error_t error;
    int rc = -1;

    if ((o = json_loads (spec, 0, &error)) == NULL) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "JSON load: %s", error.text);
        errno = EINVAL;
        return -1;
    }

    rc = jjc_get_counts_json (o, jj);
    json_decref (o);
    return rc;
}

int jjc_get_counts_json (json_t *jobspec, struct jjc_counts *jj)
{
    int version;
    json_t *resources = NULL;
    json_error_t error;

    if (!jj) {
        errno = EINVAL;
        return -1;
    }
    memset (jj, 0, sizeof (*jj));
    jj->nnodes = NULL;
    jj->nslots = NULL;
    jj->slot_size = NULL;
    jj->slot_gpus = NULL;

    if (json_unpack_ex (jobspec, &error, 0, "{s:i s:o}",
                        "version", &version,
                        "resources", &resources) < 0) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "at top level: %s", error.text);
        errno = EINVAL;
        return -1;
    }
    /* jobspec version check omitted as discussed in #6632 and #6682
     * N.B. attributes.system is generally optional, but
     * attributes.system.duration is required in jobspec version 1 */
    if (json_unpack_ex (jobspec, &error, 0, "{s:{s:{s:F}}}",
                        "attributes",
                          "system",
                            "duration", &jj->duration) < 0) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                  "at top level: getting duration: %s", error.text);
        errno = EINVAL;
        return -1;
    }
    if (jjc_read_level (resources, 0, jj, 1) < 0)
        return -1;

    if (!jj->nslots) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                 "Unable to determine slot count");
        errno = EINVAL;
        return -1;
    }
    if (!jj->slot_size) {
        snprintf (jj->error, sizeof (jj->error) - 1,
                 "Unable to determine slot size");
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void jjc_destroy (struct jjc_counts *jj)
{
    if (jj) {
        int saved_errno = errno;
        count_destroy (jj->nnodes);
        count_destroy (jj->nslots);
        count_destroy (jj->slot_size);
        count_destroy (jj->slot_gpus);
        errno = saved_errno;
    }
}

/* vi: ts=4 sw=4 expandtab
 */
