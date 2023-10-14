/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include "src/common/libutil/errno_safe.h"
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

json_t *resource_set_get_json (struct resource_set *r)
{
    return r ? r->R : NULL;
}

static int util_idset_set_string (struct idset *idset, const char *ids)
{
    struct idset *new;
    int rc = -1;

    if (!(new = idset_decode (ids)))
        return -1;
    if (idset_has_intersection (idset, new)) {
        errno = EEXIST;
        goto done;
    }
    rc = idset_add (idset, new);
done:
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
            || (util_idset_set_string (idset, ranks) < 0))
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
    /*  Default values: 0. indicates "unset"
     */
    r->expiration = 0.;
    r->starttime = 0.;
    if (json_unpack_ex (r->R, errp, 0,
                        "{s:{s?F s?F}}",
                        "execution",
                        "starttime",  &r->starttime,
                        "expiration", &r->expiration) < 0)
        return -1;
    return 0;
}

struct resource_set *resource_set_create_fromjson (json_t *R,
                                                   json_error_t *errp)
{
    int version = 0;
    struct resource_set *r = calloc (1, sizeof (*r));
    if (!r) {
        snprintf (errp->text, sizeof (errp->text), "out of memory");
        goto err;
    }
    r->R = json_incref (R);
    if (json_unpack_ex (r->R,
                        errp,
                        0,
                        "{s:i s:{s:o}}",
                        "version", &version,
                        "execution",
                        "R_lite", &r->R_lite) < 0)
        goto err;
    if (version != 1) {
        if (errp)
            snprintf (errp->text,
                      sizeof (errp->text),
                      "invalid version: %d",
                      version);
        goto err;
    }
    if (!(r->ranks = rset_ranks (r))) {
        if (errp)
            snprintf (errp->text,
                      sizeof (errp->text),
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

struct resource_set *resource_set_create (const char *R, json_error_t *errp)
{
    json_t *o;
    struct resource_set *rset;

    if (!(o = json_loads (R, 0, errp)))
        return NULL;
    rset = resource_set_create_fromjson (o, errp);
    ERRNO_SAFE_WRAP (json_decref, o);
    return rset;
}

const struct idset *resource_set_ranks (struct resource_set *r)
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

uint32_t resource_set_nth_rank (struct resource_set *r, int n)
{
    uint32_t rank;

    if (r == NULL || n < 0) {
        errno = EINVAL;
        return IDSET_INVALID_ID;
    }

    rank = idset_first (r->ranks);
    while (n-- && rank != IDSET_INVALID_ID)
        rank = idset_next (r->ranks, rank);
    if (rank == IDSET_INVALID_ID)
        errno = ENOENT;
    return rank;
}

uint32_t resource_set_rank_index (struct resource_set *r, uint32_t rank)
{
    uint32_t i, n;

    if (r == NULL) {
        errno = EINVAL;
        return IDSET_INVALID_ID;
    }

    i = 0;
    n = idset_first (r->ranks);
    while (n != IDSET_INVALID_ID) {
        if (n == rank)
            return i;
        i++;
        n = idset_next (r->ranks, n);
    }
    errno = ENOENT;
    return IDSET_INVALID_ID;
}

/* vi: ts=4 sw=4 expandtab
 */
