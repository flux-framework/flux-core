/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_TREQ_H
#define _FLUX_KVS_TREQ_H

#include <jansson.h>

typedef struct treq_mgr treq_mgr_t;

/*
 * treq_mgr_t API
 */

/* flux_t is optional, if NULL logging will go to stderr */
treq_mgr_t *treq_mgr_create (void);

void treq_mgr_destroy (treq_mgr_t *trm);

/* Add transaction into the treq manager */
int treq_mgr_add_transaction (treq_mgr_t *trm,
                              const flux_msg_t *request,
                              const char *name);

/* Lookup a transaction previously stored via
 * treq_mgr_add_transaction(), via name */
const flux_msg_t *treq_mgr_lookup_transaction (treq_mgr_t *trm, const char *name);

/* remove a transaction from the treq manager */
int treq_mgr_remove_transaction (treq_mgr_t *trm, const char *name);

/* Get count of transactions stored */
int treq_mgr_transactions_count (treq_mgr_t *trm);

#endif /* !_FLUX_KVS_TREQ_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

