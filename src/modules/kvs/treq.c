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
};

struct treq {
    char *name;
    const flux_msg_t *request;
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
        free (trm);
    }
}

int treq_mgr_add_transaction (treq_mgr_t *trm, treq_t *tr)
{
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

int treq_mgr_remove_transaction (treq_mgr_t *trm, const char *name)
{
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
        flux_msg_decref (tr->request);
        free (tr);
    }
}

treq_t *treq_create (const flux_msg_t *request,
                     uint32_t rank,
                     unsigned int seq)
{
    treq_t *tr = NULL;
    int saved_errno;

    if (!(tr = calloc (1, sizeof (*tr)))) {
        saved_errno = errno;
        goto error;
    }
    if (request) {
        if (!(tr->request = flux_msg_incref (request))) {
            saved_errno = errno;
            goto error;
        }
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

const flux_msg_t *treq_get_request (treq_t *tr)
{
    return tr->request;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
