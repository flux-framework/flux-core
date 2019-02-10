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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvssync.h"

struct kvssync {
    flux_msg_handler_f cb;
    flux_t *h;
    flux_msg_handler_t *mh;
    flux_msg_t *msg;
    void *arg;
    int seq;
};

static int kvssync_cmp (void *item1, void *item2)
{
    struct kvssync *ks1 = item1;
    struct kvssync *ks2 = item2;

    if (ks1->seq < ks2->seq)
        return -1;
    if (ks1->seq > ks2->seq)
        return 1;
    return 0;
}

static void kvssync_destroy (void *data)
{
    struct kvssync *ks = data;
    if (ks) {
        if (ks->msg)
            flux_msg_destroy (ks->msg);
        free (ks);
    }
}

int kvssync_add (struct kvsroot *root, flux_msg_handler_f cb, flux_t *h,
                 flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg,
                 int seq)
{
    struct kvssync *ks = NULL;

    if (!root || !msg || root->seq >= seq) {
        errno = EINVAL;
        goto error;
    }

    if (!(ks = calloc (1, sizeof (*ks)))) {
        errno = ENOMEM;
        goto error;
    }

    if (!(ks->msg = flux_msg_copy (msg, true))) {
        errno = ENOMEM;
        goto error;
    }

    ks->cb = cb;
    ks->h = h;
    ks->mh = mh;
    ks->arg = arg;
    ks->seq = seq;

    if (zlist_push (root->synclist, ks) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (root->synclist, ks, kvssync_destroy, false);

    zlist_sort (root->synclist, kvssync_cmp);

    return 0;

error:
    kvssync_destroy (ks);
    return -1;
}

void kvssync_process (struct kvsroot *root, bool all)
{
    struct kvssync *ks;

    if (!root)
        return;

    /* notify sync waiters that version has been reached */

    ks = zlist_first (root->synclist);
    while (ks && (all || root->seq >= ks->seq)) {
        ks = zlist_pop (root->synclist);
        ks->cb (ks->h, ks->mh, ks->msg, ks->arg);
        kvssync_destroy (ks);
        ks = zlist_first (root->synclist);
    }
}

int kvssync_remove_msg (struct kvsroot *root,
                        kvssync_test_msg_f cmp,
                        void *arg)
{
    zlist_t *tmp = NULL;
    struct kvssync *ks;
    int rc = -1;
    int saved_errno;

    if (!root || !cmp) {
        saved_errno = EINVAL;
        goto error;
    }

    ks = zlist_first (root->synclist);
    while (ks) {
        if (cmp (ks->msg, arg)) {
            if (!tmp && !(tmp = zlist_new ())) {
                saved_errno = ENOMEM;
                goto error;
            }
            if (zlist_append (tmp, ks) < 0) {
                saved_errno = ENOMEM;
                goto error;
            }
        }
        ks = zlist_next (root->synclist);
    }
    if (tmp) {
        while ((ks = zlist_pop (tmp)))
            zlist_remove (root->synclist, ks);
    }
    rc = 0;
error:
    /* if an error occurs above in zlist_new() or zlist_append(),
     * simply destroy the tmp list.  Nothing has been removed off of
     * the original queue yet.  Allow user to handle error as they see
     * fit.
     */
    zlist_destroy (&tmp);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
