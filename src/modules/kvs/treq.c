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
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"

#include "treq.h"

struct treq_mgr {
    zhash_t *transactions;
    bool iterating_transactions;
    zlist_t *removelist;
};

struct treq {
    char *name;
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
        saved_errno = errno;
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
     * iteration, so save name for removal later.
     */
    if (trm->iterating_transactions) {
        char *str = strdup (name);

        if (!str)
            return -1;

        if (zlist_append (trm->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    }
    else
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

static treq_t *treq_create_common (int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!(tr = calloc (1, sizeof (*tr)))) {
        saved_errno = errno;
        goto error;
    }
    if (!(tr->ops = json_array ())
        || !(tr->requests = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    tr->flags = flags;
    tr->processed = false;

    return tr;
error:
    treq_destroy (tr);
    errno = saved_errno;
    return NULL;
}

treq_t *treq_create (const char *name, int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!name) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(tr = treq_create_common (flags))) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(tr->name = strdup (name))) {
        saved_errno = errno;
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
                          int flags)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!(tr = treq_create_common (flags))) {
        saved_errno = EINVAL;
        goto error;
    }

    if (asprintf (&(tr->name), "treq.%u.%u", rank, seq) < 0) {
        saved_errno = errno;
        goto error;
    }

    return tr;
error:
    treq_destroy (tr);
    errno = saved_errno;
    return NULL;
}

const char *treq_get_name (treq_t *tr)
{
    return tr->name;
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

    if (ops) {
        for (i = 0; i < json_array_size (ops); i++) {
            if ((op = json_array_get (ops, i)))
                if (json_array_append (tr->ops, op) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
        }
    }
    return 0;
}

int treq_add_request_copy (treq_t *tr, const flux_msg_t *request)
{
    if (zlist_push (tr->requests, (void *)flux_msg_incref (request)) < 0) {
        flux_msg_decref (request);
        return -1;
    }
    zlist_freefn (tr->requests,
                  (void *)request,
                  (zlist_free_fn *)flux_msg_decref,
                  false);
    return 0;
}

int treq_iter_request_copies (treq_t *tr, treq_msg_cb cb, void *data)
{
    const flux_msg_t *msg;

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

void treq_mark_processed (treq_t *tr)
{
    tr->processed = true;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
