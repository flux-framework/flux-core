/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "libjj.h"

static int jj_read_level (json_t *o, int level, struct jj_counts *jj)
{
    int count;
    const char *type = NULL;
    json_t *with = NULL;
    json_error_t error;

    /* Only one item per level allowed */
    if (json_unpack_ex (o,
                        &error,
                        0,
                        "[{s:s,s:i,s?o}]",
                        "type",
                        &type,
                        "count",
                        &count,
                        "with",
                        &with)
        < 0) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "level %d: %s",
                  level,
                  error.text);
        errno = EINVAL;
        return -1;
    }
    if (count <= 0) {
        sprintf (jj->error, "Invalid count %d for type '%s'", count, type);
        errno = EINVAL;
        return -1;
    }
    if (strcmp (type, "node") == 0)
        jj->nnodes = count;
    else if (strcmp (type, "slot") == 0)
        jj->nslots = count;
    else if (strcmp (type, "core") == 0)
        jj->slot_size = count;
    else {
        sprintf (jj->error, "Invalid type '%s'", type);
        errno = EINVAL;
        return -1;
    }
    if (with)
        return jj_read_level (with, level + 1, jj);
    return 0;
}

int libjj_get_counts (const char *spec, struct jj_counts *jj)
{
    int saved_errno;
    int rc = -1;
    int version;
    json_t *resources = NULL;
    json_t *o = NULL;
    json_error_t error;

    if (!jj) {
        errno = EINVAL;
        return -1;
    }
    memset (jj, 0, sizeof (*jj));

    if ((o = json_loads (spec, 0, &error)) == NULL) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "JSON load: %s",
                  error.text);
        errno = EINVAL;
        return -1;
    }

    if (json_unpack_ex (o,
                        &error,
                        0,
                        "{s:i,s:o}",
                        "version",
                        &version,
                        "resources",
                        &resources)
        < 0) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "at top level: %s",
                  error.text);
        errno = EINVAL;
        goto err;
    }
    if (version != 1) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "Invalid version: expected 1, got %d",
                  version);
        errno = EINVAL;
        goto err;
    }
    if (jj_read_level (resources, 0, jj) < 0)
        goto err;

    if (jj->nslots <= 0) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "Unable to determine slot count");
        errno = EINVAL;
        goto err;
    }
    if (jj->slot_size <= 0) {
        snprintf (jj->error,
                  sizeof (jj->error) - 1,
                  "Unable to determine slot size");
        errno = EINVAL;
        goto err;
    }
    if (jj->nnodes)
        jj->nslots *= jj->nnodes;
    rc = 0;
err:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return rc;
}

/* vi: ts=4 sw=4 expandtab
 */
