/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include "rset.h"

struct resource_set {
    json_t *R;
    const json_t *R_lite;
    struct idset *ranks;

    double starttime;
    double expiration;
};

void resource_set_destroy (struct resource_set *r)
{
    if (r) {
        r->R_lite = NULL;
        json_decref (r->R);
        idset_destroy (r->ranks);
        free (r);
    }
}

static int idset_add_set (struct idset *set, struct idset *new)
{
    unsigned int i = idset_first (new);
    while (i != IDSET_INVALID_ID) {
        if (idset_test (set, i)) {
            errno = EEXIST;
            return -1;
        }
        if (idset_set (set, i) < 0)
            return -1;
        i = idset_next (new, i);
    }
    return 0;
}

static int idset_set_string (struct idset *idset, const char *ids)
{
    int rc;
    struct idset *new = idset_decode (ids);
    if (!new)
        return -1;
    rc = idset_add_set (idset, new);
    idset_destroy (new);
    return rc;
}

static struct idset *rset_ranks (struct resource_set *r)
{
    int i;
    json_t *entry;
    struct idset *idset = NULL;
    const char *ranks = NULL;

    if (!r || !r->R_lite) {
        errno = EINVAL;
        return NULL;
    }
    if (!(idset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    json_array_foreach (r->R_lite, i, entry) {
        if ((json_unpack_ex (entry, NULL, 0, "{s:s}", "rank", &ranks) < 0)
            || (idset_set_string (idset, ranks) < 0))
            goto err;
    }
    return idset;
err:
    idset_destroy (idset);
    return NULL;
}

static int rset_read_time_window (struct resource_set *r, json_error_t *errp)
{
    if (!r || !r->R_lite) {
        errno = EINVAL;
        return -1;
    }
    /*  Default values: < 0 indicates "unset"
     */
    r->expiration = -1.;
    r-> starttime = -1.;
    if (json_unpack_ex (r->R, errp, 0,
                        "{s:{s?F s?F}}",
                        "execution",
                        "starttime",  &r->starttime,
                        "expiration", &r->expiration) < 0)
        return -1;
    return 0;
}

struct resource_set * resource_set_create (const char *R, json_error_t *errp)
{
    int version = 0;
    struct resource_set *r = calloc (1, sizeof (*r));
    if (!(r->R = json_loads (R, 0, errp)))
        goto err;
    if (json_unpack_ex (r->R, errp, 0, "{s:i s:{s:o}}",
                                       "version", &version,
                                       "execution",
                                       "R_lite", &r->R_lite) < 0)
        goto err;
    if (version != 1) {
        if (errp)
            snprintf (errp->text, sizeof (errp->text),
                    "invalid version: %d", version);
        goto err;
    }
    if (!(r->ranks = rset_ranks (r))) {
        if (errp)
            snprintf (errp->text, sizeof (errp->text),
                    "R_lite: failed to read target rank list");
        goto err;
    }
    if (rset_read_time_window (r, errp) < 0)
        goto err;
    return (r);
err:
    resource_set_destroy (r);
    return NULL;
}

const struct idset * resource_set_ranks (struct resource_set *r)
{
    return r->ranks;
}

double resource_set_starttime (struct resource_set *r)
{
    return r->starttime;
}

double resource_set_expiration (struct resource_set *r)
{
    return r->expiration;
}


/* vi: ts=4 sw=4 expandtab
 */
