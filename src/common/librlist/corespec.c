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

#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"

#include "rlist.h"
#include "rnode.h"
#include "rlist_private.h"

struct core_spec {
    char *spec;
    struct idset *cores;
    struct idset *ranks;
};

static void core_spec_destroy (struct core_spec *spec)
{
    if (spec) {
        int saved_errno = errno;
        idset_destroy (spec->cores);
        idset_destroy (spec->ranks);
        free (spec->spec);
        free (spec);
        errno = saved_errno;
    }
}

static struct core_spec *core_spec_create (const char *s, flux_error_t *errp)
{
    idset_error_t error;
    struct core_spec *spec;
    const char *ranks = NULL;
    const char *cores;
    int len = -1;

    cores = s;
    if ((ranks = strchr (cores, '@'))) {
        len = ranks - cores;
        ranks++;
    }
    if (!(spec = calloc (1, sizeof (*spec)))
        || !(spec->spec = strdup (s))) {
        errprintf (errp, "Out of memory");
        goto error;
    }
    if ((ranks && !(spec->ranks = idset_decode_ex (ranks, -1, 0, 0, &error)))
        || !(spec->cores = idset_decode_ex (cores, len, 0, 0, &error))) {
        errprintf (errp, "%s", error.text);
        goto error;
    }
    if ((spec->ranks && idset_count (spec->ranks) == 0)
        || idset_count (spec->cores) == 0) {
        errprintf (errp, "ranks/cores cannot be empty");
        goto error;
    }
    return spec;
error:
    core_spec_destroy (spec);
    return NULL;
}

static struct rnode *core_spec_copy (const struct rnode *orig,
                                     struct core_spec *spec)
{
    struct rnode *n = NULL;

    /* If spec->ranks is NULL, this indicates all ranks
     */
    if (!spec->ranks || idset_test (spec->ranks, orig->rank)) {
        /* Create new rnode object with just the cores intersection, keeping
         * hostname and properties.
         */
        struct idset *ids = idset_intersect (orig->cores->ids, spec->cores);
        if (ids != NULL
            && idset_count (ids) > 0
            && (n = rnode_create_idset (orig->hostname, orig->rank, ids))) {
            n->properties = zhashx_dup (orig->properties);
        }
        idset_destroy (ids);
    }
    return n;
}

static void core_spec_destructor (void **item)
{
    if (item) {
        struct core_spec *spec = *item;
        core_spec_destroy (spec);
        *item = NULL;
    }
}

static zlistx_t *core_spec_list_create (const char *core_spec,
                                        flux_error_t *errp)
{
    char *copy;
    char *str;
    char *spec;
    char *sp = NULL;
    zlistx_t *l = zlistx_new ();

    if (!l || !(copy = strdup (core_spec)))
        return NULL;
    str = copy;

    zlistx_set_destructor (l, core_spec_destructor);

    while ((spec = strtok_r (str, " \t", &sp))) {
        struct core_spec *cspec;
        if (!(cspec = core_spec_create (spec, errp)))
            goto error;
        if (!zlistx_add_end (l, cspec)) {
            errprintf (errp, "Out of memory");
            goto error;
        }
        str = NULL;
    }
    free (copy);
    return l;
error:
    free (copy);
    zlistx_destroy (&l);
    return NULL;
}

struct rlist *rlist_copy_core_spec (const struct rlist *orig,
                                    const char *core_spec,
                                    flux_error_t *errp)
{
    struct core_spec *spec;
    struct rlist *rl = NULL;
    zlistx_t *l;

    if (!(l = core_spec_list_create (core_spec, errp)))
        return NULL;

    spec = zlistx_first (l);
    while (spec) {
        struct rlist *tmp;
        if (!(tmp = rlist_copy_internal (orig,
                                         (rnode_copy_f) core_spec_copy,
                                         (void *) spec))) {
            errprintf (errp, "failed to copy spec '%s'", spec->spec);
            goto error;
        }
        if (rl != NULL) {
            if (rlist_add (rl, tmp) < 0) {
                errprintf (errp,
                           "rlist_add '%s' failed: %s",
                           spec->spec,
                           strerror (errno));
                goto error;
            }
            rlist_destroy (tmp);
        }
        else
            rl = tmp;
        spec = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return rl;
error:
    rlist_destroy (rl);
    zlistx_destroy (&l);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
