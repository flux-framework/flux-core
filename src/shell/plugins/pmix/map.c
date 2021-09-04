/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* map.c - generate map strings required to be set in pmix server nspace
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <argz.h>
#include <jansson.h>
#include <pmix_server.h>

#include "src/common/libhostlist/hostlist.h"
#include "src/common/librlist/rlist.h"

#include "rcalc.h"

/* Create a comma-separated list of hosts from 'hl' for input to
 * PMIx_generate_regex().  IOW, like hostlist_encode() w/o range compression.
 */
static char *csv_from_hostlist (struct hostlist *hl)
{
    char *argz = NULL;
    size_t argz_len = 0;
    const char *host;

    host = hostlist_first (hl);
    while (host) {
        if (argz_add (&argz, &argz_len, host) != 0) {
            free (argz);
            return NULL;
        }
        host = hostlist_next (hl);
    }
    argz_stringify (argz, argz_len, ',');
    return argz;
}

char *pp_map_node_create (json_t *R)
{
    struct rlist *rl = NULL;
    struct hostlist *hl = NULL;
    char *csv = NULL;
    char *value;

    if (!(rl = rlist_from_json (R, NULL))
        || !(hl = rlist_nodelist (rl))
        || !(csv = csv_from_hostlist (hl))
        || PMIx_generate_regex (csv, &value) != PMIX_SUCCESS)
        value = NULL;

    free (csv);
    rlist_destroy (rl);
    hostlist_destroy (hl);
    return value;
}

/* Create a comma-separated list of ranks for tasks running on
 * the node described by 'ri'.
 */
static char *rankset_create (struct rcalc_rankinfo *ri)
{
    struct idset *ids;
    char *value;

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (ids,
                            ri->global_basis,
                            ri->global_basis + ri->ntasks - 1) < 0
        || !(value = idset_encode (ids, 0)))
        value = NULL;
    idset_destroy (ids);
    return value;
}

char *pp_map_proc_create (int nnodes, rcalc_t *rcalc)
{
    char *argz = NULL;
    size_t argz_len = 0;
    char *value;

    for (int i = 0; i < nnodes; i++) {
        struct rcalc_rankinfo ri;
        char *rankset = NULL;

        if (rcalc_get_nth (rcalc, i, &ri) < 0
            || !(rankset = rankset_create (&ri))
            || argz_add (&argz, &argz_len, rankset) != 0) {
            free (rankset);
            free (argz);
            return NULL;
        }
    }
    argz_stringify (argz, argz_len, ';');
    if (PMIx_generate_ppn (argz, &value) != PMIX_SUCCESS)
        value = NULL;
    free (argz);
    return value;
}

char *pp_map_local_peers (int shell_rank, rcalc_t *rcalc)
{
    struct rcalc_rankinfo ri;
    if (rcalc_get_nth (rcalc, shell_rank, &ri) < 0)
        return NULL;
    return rankset_create (&ri);
}

// vi:ts=4 sw=4 expandtab
