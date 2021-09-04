/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* synchro.c - synchronize with simple PMIx server functions
 *
 * Some PMIx server function calls are asynchronous, with completion
 * status returned to a pmix_op_cbfunc_t callback made in PMIx server
 * thread context.  The 'synchro' mini-class provides a way for these
 * functions to be called *synchronously* without going through the
 * server socket in server.c.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <pthread.h>
#include <pmix_server.h>

#include "synchro.h"

void pp_synchro_init (struct synchro *x)
{
    pthread_mutex_init (&x->lock, NULL);
    pthread_cond_init (&x->cond, NULL);
    x->valid = 0;
}

void pp_synchro_clear (struct synchro *x)
{
    pthread_mutex_lock (&x->lock);
    x->valid = 0;
    pthread_mutex_unlock (&x->lock);
}

/* pmix_op_cbfunc_t function signature */
void pp_synchro_signal (pmix_status_t status, void *cbdata)
{
    struct synchro *x = cbdata;

    pthread_mutex_lock (&x->lock);
    x->status = status;
    x->valid = 1;
    pthread_cond_signal (&x->cond);
    pthread_mutex_unlock (&x->lock);
}

pmix_status_t pp_synchro_wait (struct synchro *x)
{
    pmix_status_t rc = PMIX_ERROR;

    pthread_mutex_lock (&x->lock);
    while (!x->valid)
        pthread_cond_wait (&x->cond, &x->lock);
    rc = x->status;
    pthread_mutex_unlock (&x->lock);

    return rc;
}

// vi:ts=4 sw=4 expandtab
