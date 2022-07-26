/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_CHECKPOINT_H
#define _FLUX_KVS_CHECKPOINT_H

#include <flux/core.h>

#include "kvsroot.h"

/* kvs_checkpoint will handle checkpointing for the checkpoint-period
 * configuration under the [kvs] table.  Internally the checkpoint-period
 * value and a timer are managed.
 *
 * To avoid excess comparisons for `rank == 0` throughout KVS code,
 * most functions below are no-ops if the `kvs_checkpoint_t` argument
 * is NULL.
 */

typedef struct kvs_checkpoint kvs_checkpoint_t;

/* callback after sync/checkpoint transaction submitted */
typedef void (*kvs_checkpoint_txn_cb)(struct kvsroot *root, void *arg);

/* root_primary - root of primary namespace, will be passed to txn_cb
 *              - can be NULL if not available at creation time, use
 *                kvs_checkpoint_update_root_primary() to set later.
 * checkpoint_period - timer will trigger a checkpoint every X seconds,
 *                   - no timer will be done if <= 0.0.
 * txn_cb - callback after each checkpoint transaction submitted
 * txn_cb_arg - passed to txn_cb
 */
kvs_checkpoint_t *kvs_checkpoint_create (flux_t *h,
                                         struct kvsroot *root_primary,
                                         double checkpoint_period,
                                         kvs_checkpoint_txn_cb txn_cb,
                                         void *txn_cb_arg);

/* update internal checkpoint_period setting as needed */
int kvs_checkpoint_config_parse (kvs_checkpoint_t *kcp,
                                 const flux_conf_t *conf,
                                 flux_error_t *errp);

/* update internal checkpoint_period setting as needed and restart
 * internal timers if needed
 */
int kvs_checkpoint_reload (kvs_checkpoint_t *kcp,
                           const flux_conf_t *conf,
                           flux_error_t *errp);

/* update kvsroot used internally */
void kvs_checkpoint_update_root_primary (kvs_checkpoint_t *kcp,
                                         struct kvsroot *root_primary);

/* start / restart checkpoint timer.  If root_primary not yet set or
 * checkpoint_period <= 0.0, will do nothing.
 */
void kvs_checkpoint_start (kvs_checkpoint_t *kcp);

void kvs_checkpoint_destroy (kvs_checkpoint_t *kcp);


#endif /* !_FLUX_KVS_CHECKPOINT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
