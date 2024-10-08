/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"

#include "kvs_checkpoint.h"
#include "kvsroot.h"

struct kvs_checkpoint {
    flux_t *h;
    struct kvsroot *root_primary;
    double checkpoint_period;   /* in seconds */
    flux_watcher_t *checkpoint_w;
    kvs_checkpoint_txn_cb txn_cb;
    void *txn_cb_arg;
    int last_checkpoint_seq;
};

static int checkpoint_period_parse (const flux_conf_t *conf,
                                    flux_error_t *errp,
                                    double *checkpoint_period)
{
    flux_error_t error;
    const char *str = NULL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s}}",
                          "kvs",
                          "checkpoint-period", &str) < 0) {
        errprintf (errp, "error reading config for kvs: %s", error.text);
        return -1;
    }

    if (str) {
        if (fsd_parse_duration (str, checkpoint_period) < 0) {
            errprintf (errp, "invalid checkpoint-period config: %s", str);
            return -1;
        }
    }

    return 0;
}

int kvs_checkpoint_config_parse (kvs_checkpoint_t *kcp,
                                 const flux_conf_t *conf,
                                 flux_error_t *errp)
{
    if (kcp) {
        double checkpoint_period = kcp->checkpoint_period;
        if (checkpoint_period_parse (conf, errp, &checkpoint_period) < 0)
            return -1;
        kcp->checkpoint_period = checkpoint_period;
    }
    return 0;
}

int kvs_checkpoint_reload (kvs_checkpoint_t *kcp,
                           const flux_conf_t *conf,
                           flux_error_t *errp)
{
    if (kcp) {
        double checkpoint_period = kcp->checkpoint_period;
        if (checkpoint_period_parse (conf,
                                     errp,
                                     &checkpoint_period) < 0)
            return -1;

        if (checkpoint_period != kcp->checkpoint_period) {
            kcp->checkpoint_period = checkpoint_period;
            flux_watcher_stop (kcp->checkpoint_w);

            if (kcp->root_primary
                && kcp->checkpoint_period > 0.0) {
                flux_timer_watcher_reset (kcp->checkpoint_w,
                                          kcp->checkpoint_period,
                                          kcp->checkpoint_period);
                flux_watcher_start (kcp->checkpoint_w);
            }
        }
    }
    return 0;
}


static void checkpoint_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    kvs_checkpoint_t *kcp = arg;
    char name[64];
    json_t *ops = NULL;

    /* if no changes to root since last checkpoint-period, do
     * nothing */
    if (kcp->last_checkpoint_seq == kcp->root_primary->seq)
        return;

    snprintf (name,
              sizeof (name),
              "checkpoint-period.%u",
              kcp->root_primary->seq);

    if (!(ops = json_array ())) {
        errno = ENOMEM;
        flux_log_error (kcp->h, "checkpoint-period setup failure");
        goto done;
    }

    /* Set FLUX_KVS_SYNC, to perform the checkpoint.
     *
     * Set KVSTXN_INTERNAL_FLAG_NO_PUBLISH, this is an internal KVS
     * module transaction to checkpoint.  It has no operations so the
     * KVS data will not change.  Therefore no setroot() needs to be
     * called after this is done.
     */
    if (kvstxn_mgr_add_transaction (kcp->root_primary->ktm,
                                    name,
                                    ops,
                                    FLUX_KVS_SYNC,
                                    KVSTXN_INTERNAL_FLAG_NO_PUBLISH) < 0) {
        flux_log_error (kcp->h, "%s: kvstxn_mgr_add_transaction", __FUNCTION__);
        goto done;
    }

    if (kcp->txn_cb)
        kcp->txn_cb (kcp->root_primary, kcp->txn_cb_arg);

    /* N.B. "last_checkpoint_seq" protects against unnecessary
     * checkpointing when there is no activity in the primary KVS.
     */
    kcp->last_checkpoint_seq = kcp->root_primary->seq;

done:
    json_decref (ops);
}

kvs_checkpoint_t *kvs_checkpoint_create (flux_t *h,
                                         struct kvsroot *root_primary,
                                         double checkpoint_period,
                                         kvs_checkpoint_txn_cb txn_cb,
                                         void *txn_cb_arg)
{
    kvs_checkpoint_t *kcp = NULL;

    if (!(kcp = calloc (1, sizeof (*kcp))))
        goto error;

    kcp->h = h;
    kcp->root_primary = root_primary; /* can be NULL initially */
    kcp->checkpoint_period = checkpoint_period;
    kcp->txn_cb = txn_cb;
    kcp->txn_cb_arg = txn_cb_arg;

    /* create regardless of checkpoint-period value, in case user
     * reconfigures later.
     */
    if (!(kcp->checkpoint_w =
          flux_timer_watcher_create (flux_get_reactor (h),
                                     kcp->checkpoint_period,
                                     kcp->checkpoint_period,
                                     checkpoint_cb,
                                     kcp))) {
        flux_log_error (kcp->h, "flux_timer_watcher_create");
        goto error;

    }

    return kcp;

 error:
    kvs_checkpoint_destroy (kcp);
    return NULL;
}

void kvs_checkpoint_update_root_primary (kvs_checkpoint_t *kcp,
                                         struct kvsroot *root_primary)
{
    if (kcp && root_primary)
        kcp->root_primary = root_primary;
}

void kvs_checkpoint_start (kvs_checkpoint_t *kcp)
{
    if (kcp
        && kcp->root_primary
        && kcp->checkpoint_period > 0.0) {
        flux_watcher_stop (kcp->checkpoint_w);
        flux_timer_watcher_reset (kcp->checkpoint_w,
                                  kcp->checkpoint_period,
                                  kcp->checkpoint_period);
        flux_watcher_start (kcp->checkpoint_w);
    }
}

void kvs_checkpoint_destroy (kvs_checkpoint_t *kcp)
{
    if (kcp) {
        int save_errno = errno;
        flux_watcher_destroy (kcp->checkpoint_w);
        free (kcp);
        errno = save_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
