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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "kvs_wait_version.h"

struct kvs_wait_version {
    flux_msg_handler_f cb;
    flux_t *h;
    flux_msg_handler_t *mh;
    const flux_msg_t *msg;
    void *arg;
    int seq;
};

static int kvs_wait_version_cmp (void *item1, void *item2)
{
    struct kvs_wait_version *ks1 = item1;
    struct kvs_wait_version *ks2 = item2;

    if (ks1->seq < ks2->seq)
        return -1;
    if (ks1->seq > ks2->seq)
        return 1;
    return 0;
}

static void kvs_wait_version_destroy (void *data)
{
    struct kvs_wait_version *ks = data;
    if (ks) {
        flux_msg_decref (ks->msg);
        free (ks);
    }
}

int kvs_wait_version_add (struct kvsroot *root,
                          flux_msg_handler_f cb,
                          flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg,
                          int seq)
{
    struct kvs_wait_version *kwv = NULL;

    if (!root || !msg || root->seq >= seq) {
        errno = EINVAL;
        goto error;
    }

    if (!(kwv = calloc (1, sizeof (*kwv))))
        goto error;

    kwv->msg = flux_msg_incref (msg);
    kwv->cb = cb;
    kwv->h = h;
    kwv->mh = mh;
    kwv->arg = arg;
    kwv->seq = seq;

    if (zlist_push (root->wait_version_list, kwv) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (root->wait_version_list,
                  kwv,
                  kvs_wait_version_destroy,
                  false);

    zlist_sort (root->wait_version_list, kvs_wait_version_cmp);

    return 0;

 error:
    kvs_wait_version_destroy (kwv);
    return -1;
}

void kvs_wait_version_process (struct kvsroot *root, bool all)
{
    struct kvs_wait_version *kwv;

    if (!root)
        return;

    /* notify sync waiters that version has been reached */

    kwv = zlist_first (root->wait_version_list);
    while (kwv && (all || root->seq >= kwv->seq)) {
        kwv = zlist_pop (root->wait_version_list);
        kwv->cb (kwv->h, kwv->mh, kwv->msg, kwv->arg);
        kvs_wait_version_destroy (kwv);
        kwv = zlist_first (root->wait_version_list);
    }
}

int kvs_wait_version_remove_msg (struct kvsroot *root,
                                 kvs_wait_version_test_msg_f cmp,
                                 void *arg)
{
    zlist_t *tmp = NULL;
    struct kvs_wait_version *kwv;
    int rc = -1;
    int saved_errno;

    if (!root || !cmp) {
        saved_errno = EINVAL;
        goto error;
    }

    kwv = zlist_first (root->wait_version_list);
    while (kwv) {
        if (cmp (kwv->msg, arg)) {
            if (!tmp && !(tmp = zlist_new ())) {
                saved_errno = ENOMEM;
                goto error;
            }
            if (zlist_append (tmp, kwv) < 0) {
                saved_errno = ENOMEM;
                goto error;
            }
        }
        kwv = zlist_next (root->wait_version_list);
    }
    if (tmp) {
        while ((kwv = zlist_pop (tmp)))
            zlist_remove (root->wait_version_list, kwv);
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
