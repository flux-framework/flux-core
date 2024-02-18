/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* proctable.c - compressed, JSON encoded MPIR_proctable
 *
 * An MPIR proctable is an array of MPIR_PROCDESC entries, including
 * a taskid, hostname, executable name, and PID for every task in
 * parallel job. In order to reduce the transfer of data back to a
 * frontend command, the MPIR proctable is encoded by the job shell
 * using the compression techniques in rangelist.c and nodelist.c.
 *
 * The shell simply encodes the proctable as 4 separate lists, each
 * encoded in the same order:
 *
 *  - nodes: the list of hostnames in 'nodelist' form
 *  - executables: the list of executables in 'nodelist' form
 *  - taskids: the list of task ids in 'rangelist' form
 *  - pids: the list of process ids in 'rangelist' form
 *  - ranks: (not used in MPIR_proctable) list of broker ranks
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "mpir/rangelist.h"
#include "mpir/nodelist.h"
#include "mpir/proctable.h"

struct proctable {
    zhashx_t *strings;

    MPIR_PROCDESC *mpir_proctable;

    struct nodelist  *nodes;
    struct nodelist  *executables;
    struct rangelist *taskids;
    struct rangelist *pids;
    struct rangelist *ranks;
};

void proctable_destroy (struct proctable *p)
{
    if (p) {
        nodelist_destroy (p->nodes);
        nodelist_destroy (p->executables);
        rangelist_destroy (p->taskids);
        rangelist_destroy (p->pids);
        rangelist_destroy (p->ranks);
        free (p->mpir_proctable);
        if (p->strings)
            zhashx_destroy (&p->strings);
        free (p);
    }
}

static struct proctable *proctable_alloc (void)
{
    struct proctable *p;
    if (!(p = calloc (1, sizeof (*p)))
        || !(p->strings = zhashx_new ())) {
        proctable_destroy (p);
        return NULL;
    }
    /*  The strings hash is used to deduplicate strings such as
     *   host and executable names in the final MPIR_proctable,
     *   and stores the string as both key and value in the hash
     *   for convenience. To avoid unnecessary `strdup()` disable
     *   the internal zhashx key duplicator (but still leave
     *   the default key destructor as `free()` so that all strings
     *   are freed on zhashx destruction.
     */
    zhashx_set_key_duplicator (p->strings, NULL);
    return p;
}

struct proctable * proctable_create (void)
{
    struct proctable *p = proctable_alloc ();
    if (p == NULL
        || !(p->nodes = nodelist_create ())
        || !(p->executables = nodelist_create ())
        || !(p->taskids = rangelist_create ())
        || !(p->pids = rangelist_create ())
        || !(p->ranks = rangelist_create ())) {
        proctable_destroy (p);
        return NULL;
    }
   return p;
}

int proctable_append_task (struct proctable *p,
                           int broker_rank,
                           const char *hostname,
                           const char *executable,
                           int taskid,
                           pid_t pid)
{
    if (nodelist_append (p->nodes, hostname) < 0
        || nodelist_append (p->executables, executable) < 0
        || rangelist_append (p->taskids, taskid) < 0
        || rangelist_append (p->pids, pid) < 0
        || rangelist_append (p->ranks, broker_rank) < 0)
        return -1;
    return 0;
}

int proctable_append_proctable_destroy (struct proctable *p1,
                                        struct proctable *p2)
{
    if (nodelist_append_list_destroy (p1->nodes, p2->nodes) < 0
        || nodelist_append_list_destroy (p1->executables, p2->executables) < 0
        || rangelist_append_list (p1->taskids, p2->taskids) < 0
        || rangelist_append_list (p1->pids, p2->pids) < 0)
        return -1;
    if (p1->ranks
        && p2->ranks
        && rangelist_append_list (p1->ranks, p2->ranks) < 0)
        return -1;
    p2->nodes = NULL;
    p2->executables = NULL;
    proctable_destroy (p2);
    return 0;
}

struct proctable * proctable_from_json (json_t *o)
{
    json_t *nodes;
    json_t *exe;
    json_t *taskids;
    json_t *pids;
    json_t *ranks;
    struct proctable *p;

    if (json_unpack (o, "{so so so so s?o}",
                        "ids", &taskids,
                        "executables", &exe,
                        "hosts", &nodes,
                        "pids", &pids,
                        "ranks", &ranks) < 0)
        return NULL;
    if (!(p = proctable_alloc ()))
        return NULL;
    if (!(p->nodes = nodelist_from_json (nodes))
        || !(p->executables = nodelist_from_json (exe))
        || !(p->taskids = rangelist_from_json (taskids))
        || !(p->pids = rangelist_from_json (pids))
        || (ranks && !(p->ranks = rangelist_from_json (ranks)))) {
        proctable_destroy (p);
        return NULL;
    }
    return p;
}

struct proctable * proctable_from_json_string (const char *s)
{
    json_error_t err;
    struct proctable *p = NULL;
    json_t *o = json_loads (s, 0, &err);
    if (!o || !(p = proctable_from_json (o)))
        return NULL;
    json_decref (o);
    return p;
}

json_t *proctable_to_json (struct proctable *p)
{
    json_t *o = json_object ();
    json_t *x = NULL;
    if (!o)
        return NULL;
    if (!(x = nodelist_to_json (p->nodes))
        || json_object_set_new (o, "hosts", x) < 0
        || !(x = nodelist_to_json (p->executables))
        || json_object_set_new (o, "executables", x) < 0
        || !(x = rangelist_to_json (p->taskids))
        || json_object_set_new (o, "ids", x) < 0
        || !(x = rangelist_to_json (p->pids))
        || json_object_set_new (o, "pids", x) < 0
        || !(x = rangelist_to_json (p->ranks))
        || json_object_set_new (o, "ranks", x) < 0) {
        json_decref (x);
        json_decref (o);
        return NULL;
    }
    return o;
}

int proctable_first_task (const struct proctable *p)
{
    if (!p)
        return -1;
    return rangelist_first (p->taskids);
}

/*  Cache the string `s` in proctable strings hash if not already cached.
 *   If the string is already stored in strings hash, return the cached
 *   copy and free the incoming string.
 */
static char * proctable_cache_string (struct proctable *p, char *s)
{
    char *copy;
    if (!s)
        return NULL;
    if (!(copy = zhashx_lookup (p->strings, s))) {
        zhashx_insert (p->strings, s, s);
        copy = s;
        s = NULL;
    }
    free (s);
    return copy;
}

static MPIR_PROCDESC *mpir_proctable_create (struct proctable *p)
{
    char *host;
    char *exe;
    int64_t id;
    int64_t pid;
    int size;
    MPIR_PROCDESC *table = NULL;

    if (!p || (size = rangelist_size (p->taskids)) <= 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(table = calloc (size, sizeof (MPIR_PROCDESC))))
        return NULL;

    host = nodelist_first (p->nodes);
    exe = nodelist_first (p->executables);
    id = rangelist_first (p->taskids);
    pid = rangelist_first (p->pids);

    for (int i = 0; i < size; i++) {
        if (!host || !exe || id < 0 || pid <= 0)
            goto err;

        table[id] = (MPIR_PROCDESC) {
            .host_name = proctable_cache_string (p, host),
            .executable_name = proctable_cache_string (p, exe),
            .pid = (int) pid
        };

        host = nodelist_next (p->nodes);
        exe = nodelist_next (p->executables);
        id = rangelist_next (p->taskids);
        pid = rangelist_next (p->pids);
    }
    return table;
err:
    free (table);
    return NULL;
}

MPIR_PROCDESC *proctable_get_mpir_proctable (struct proctable *p, int *sizep)
{
    if (!p->mpir_proctable)
        p->mpir_proctable = mpir_proctable_create (p);
    if (sizep)
        *sizep = proctable_get_size (p);
    return p->mpir_proctable;
}

int proctable_get_size (struct proctable *p)
{
    return rangelist_size (p->taskids);
}

struct idset *proctable_get_ranks (struct proctable *p,
                                   const struct idset *taskids)
{
    struct idset *result;
    int taskid = 0;
    int64_t rank;
    if (!p->ranks) {
        errno = ENOSYS;
        return NULL;
    }
    if (!(result = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    rank = rangelist_first (p->ranks);
    while (rank != RANGELIST_END) {
        if (!taskids || idset_test (taskids, taskid))
            if (idset_set (result, (unsigned int) rank) < 0)
                goto error;
        rank = rangelist_next (p->ranks);
        taskid++;
    }
    return result;
error:
    idset_destroy (result);
    return NULL;
}

int proctable_get_broker_rank (struct proctable *p, int taskid)
{
    int id = 0;
    int64_t rank;
    if (!p->ranks) {
        errno = ENOSYS;
        return -1;
    }
    rank = rangelist_first (p->ranks);
    while (rank != RANGELIST_END) {
        if (id == taskid)
            return (int) rank;
        rank = rangelist_next (p->ranks);
        id++;
    }
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
