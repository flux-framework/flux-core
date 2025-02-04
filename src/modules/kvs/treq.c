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

int treq_mgr_add_transaction (treq_mgr_t *trm,
                              const flux_msg_t *request,
                              const char *name)
{
    if (zhash_insert (trm->transactions,
                      name,
                      (void *)flux_msg_incref (request)) < 0) {
        flux_msg_decref (request);
        errno = EEXIST;
        return -1;
    }

    zhash_freefn (trm->transactions,
                  name,
                  (zhash_free_fn *)flux_msg_decref);
    return 0;
}

const flux_msg_t *treq_mgr_lookup_transaction (treq_mgr_t *trm, const char *name)
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
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
