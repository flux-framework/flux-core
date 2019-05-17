/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "treq.h"

struct treq_mgr {
    zhash_t *transactions;
    bool iterating_transactions;
    zlist_t *removelist;
};

struct treq {
    char *name;
    int nprocs;
    int count;
    zlist_t *requests;
    json_t *ops;
    int flags;
    bool processed;
};

/*
 * treq_mgr_t functions
 */

treq_mgr_t *treq_mgr_create (void)
{
    treq_mgr_t *trm = NULL;
    int saved_errno;

    if (!(trm = calloc (1, sizeof (*trm)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(trm->transactions = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    trm->iterating_transactions = false;
    if (!(trm->removelist = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    return trm;

error:
    treq_mgr_destroy (trm);
    errno = saved_errno;
    return NULL;
}

void treq_mgr_destroy (treq_mgr_t *trm)
{
    if (trm) {
        if (trm->transactions)
            zhash_destroy (&trm->transactions);
        if (trm->removelist)
            zlist_destroy (&trm->removelist);
        free (trm);
    }
}

int treq_mgr_add_transaction (treq_mgr_t *trm, treq_t *tr)
{
    /* Don't modify hash while iterating */
    if (trm->iterating_transactions) {
        errno = EAGAIN;
        goto error;
    }

    if (zhash_insert (trm->transactions, tr->name, tr) < 0) {
        errno = EEXIST;
        goto error;
    }

    zhash_freefn (trm->transactions,
                  treq_get_name (tr),
                  (zhash_free_fn *)treq_destroy);
    return 0;
error:
    return -1;
}

treq_t *treq_mgr_lookup_transaction (treq_mgr_t *trm, const char *name)
{
    return zhash_lookup (trm->transactions, name);
}

int treq_mgr_iter_transactions (treq_mgr_t *trm, treq_itr_f cb, void *data)
{
    treq_t *tr;
    char *name;

    trm->iterating_transactions = true;

    tr = zhash_first (trm->transactions);
    while (tr) {
        if (cb (tr, data) < 0)
            goto error;

        tr = zhash_next (trm->transactions);
    }

    trm->iterating_transactions = false;

    while ((name = zlist_pop (trm->removelist))) {
        treq_mgr_remove_transaction (trm, name);
        free (name);
    }

    return 0;

error:
    while ((name = zlist_pop (trm->removelist)))
        free (name);
    trm->iterating_transactions = false;
    return -1;
}

int treq_mgr_remove_transaction (treq_mgr_t *trm, const char *name)
{
    /* it's dangerous to remove if we're in the middle of an
     * interation, so save name for removal later.
     */
    if (trm->iterating_transactions) {
        char *str = strdup (name);

        if (!str) {
            errno = ENOMEM;
            return -1;
        }

        if (zlist_append (trm->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    } else
        zhash_delete (trm->transactions, name);
    return 0;
}

int treq_mgr_transactions_count (treq_mgr_t *trm)
{
    return zhash_size (trm->transactions);
}

/*
 * treq_t functions
 */

void treq_destroy (treq_t *tr)
{
    if (tr) {
        free (tr->name);
        json_decref (tr->ops);
        zlist_destroy (&tr->requests);
        free (tr);
    }
}

static treq_t *treq_create_common (int nprocs, int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (nprocs <= 0) {
        saved_errno = EINVAL;
        goto error;
    }
    if (!(tr = calloc (1, sizeof (*tr))) || !(tr->ops = json_array ())
        || !(tr->requests = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    tr->nprocs = nprocs;
    tr->flags = flags;
    tr->processed = false;

    return tr;
error:
    treq_destroy (tr);
    errno = saved_errno;
    return NULL;
}

treq_t *treq_create (const char *name, int nprocs, int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!name) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(tr = treq_create_common (nprocs, flags))) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(tr->name = strdup (name))) {
        saved_errno = ENOMEM;
        goto error;
    }

    return tr;
error:
    treq_destroy (tr);
    errno = saved_errno;
    return NULL;
}

treq_t *treq_create_rank (uint32_t rank,
                          unsigned int seq,
                          int nprocs,
                          int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!(tr = treq_create_common (nprocs, flags))) {
        saved_errno = EINVAL;
        goto error;
    }

    if (asprintf (&(tr->name), "treq.%u.%u", rank, seq) < 0) {
        saved_errno = ENOMEM;
        goto error;
    }

    return tr;
error:
    treq_destroy (tr);
    errno = saved_errno;
    return NULL;
}

bool treq_count_reached (treq_t *tr)
{
    assert (tr->count <= tr->nprocs);
    return (tr->count == tr->nprocs);
}

const char *treq_get_name (treq_t *tr)
{
    return tr->name;
}

int treq_get_nprocs (treq_t *tr)
{
    return tr->nprocs;
}

int treq_get_flags (treq_t *tr)
{
    return tr->flags;
}

json_t *treq_get_ops (treq_t *tr)
{
    return tr->ops;
}

int treq_add_request_ops (treq_t *tr, json_t *ops)
{
    json_t *op;
    int i;

    if (tr->count == tr->nprocs) {
        errno = EOVERFLOW;
        return -1;
    }

    if (ops) {
        for (i = 0; i < json_array_size (ops); i++) {
            if ((op = json_array_get (ops, i)))
                if (json_array_append (tr->ops, op) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
        }
    }
    tr->count++;
    return 0;
}

int treq_add_request_copy (treq_t *tr, const flux_msg_t *request)
{
    flux_msg_t *cpy = flux_msg_copy (request, false);
    if (!cpy)
        return -1;
    if (zlist_push (tr->requests, cpy) < 0) {
        flux_msg_destroy (cpy);
        return -1;
    }
    zlist_freefn (tr->requests, cpy, (zlist_free_fn *)flux_msg_destroy, false);
    return 0;
}

int treq_iter_request_copies (treq_t *tr, treq_msg_cb cb, void *data)
{
    flux_msg_t *msg;

    msg = zlist_first (tr->requests);
    while (msg) {
        if (cb (tr, msg, data) < 0)
            return -1;
        msg = zlist_next (tr->requests);
    }

    return 0;
}

bool treq_get_processed (treq_t *tr)
{
    return tr->processed;
}

void treq_set_processed (treq_t *tr, bool p)
{
    tr->processed = p;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
