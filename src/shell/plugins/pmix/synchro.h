/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <pthread.h>
#include <pmix_server.h>

#ifndef _PMIX_PP_SYNCHRO_H
#define _PMIX_PP_SYNCHRO_H

struct synchro {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    volatile pmix_status_t status;
    volatile int valid;
};

void pp_synchro_init (struct synchro *x);
void pp_synchro_clear (struct synchro *x);

pmix_status_t pp_synchro_wait (struct synchro *x);

// pmix_op_cbfunc_t function signature
void pp_synchro_signal (pmix_status_t status, void *cbdata);

#endif

// vi:ts=4 sw=4 expandtab
