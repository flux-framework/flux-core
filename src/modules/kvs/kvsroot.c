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
#include <ctype.h>
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "kvsroot.h"

struct kvsroot_mgr {
    zhashx_t *roothash;
    zlistx_t *removelist;
    bool iterating_roots;
    flux_t *h;
    void *arg;
};

struct kvs_wait_version {
    flux_msg_handler_f cb;
    flux_t *h;
    flux_msg_handler_t *mh;
    const flux_msg_t *msg;
    void *arg;
    int seq;
};

static void kvsroot_destroy (void **data);
static void kvs_wait_version_destroy (void **data);
static int kvs_wait_version_cmp (void *item1, void *item2);

kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg)
{
    kvsroot_mgr_t *krm = NULL;

    if (!(krm = calloc (1, sizeof (*krm))))
        goto error;
    if (!(krm->roothash = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (krm->roothash, kvsroot_destroy);
    if (!(krm->removelist = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_duplicator (krm->removelist, (zlistx_duplicator_fn *)strdup);
    krm->iterating_roots = false;
    krm->h = h;
    krm->arg = arg;
    return krm;

 error:
    kvsroot_mgr_destroy (krm);
    return NULL;
}

void kvsroot_mgr_destroy (kvsroot_mgr_t *krm)
{
    if (krm) {
        int save_errno = errno;
        if (krm->roothash)
            zhashx_destroy (&krm->roothash);
        if (krm->removelist)
            zlistx_destroy (&krm->removelist);
        free (krm);
        errno = save_errno;
    }
}

int kvsroot_mgr_root_count (kvsroot_mgr_t *krm)
{
    return zhashx_size (krm->roothash);
}

/* zhashx_destructor_fn */
static void kvsroot_destroy (void **data)
{
    if (data) {
        struct kvsroot *root = *data;
        int save_errno = errno;
        if (root->ns_name)
            free (root->ns_name);
        if (root->ktm)
            kvstxn_mgr_destroy (root->ktm);
        if (root->transaction_requests)
            zhashx_destroy (&(root->transaction_requests));
        if (root->wait_version_list)
            zlistx_destroy (&root->wait_version_list);
        if (root->setroot_queue)
            flux_msglist_destroy (root->setroot_queue);
        free (root);
        errno = save_errno;
    }
}

/* zhashx_destructor_fn */
static void flux_msg_decref_wrapper (void **item)
{
    flux_msg_t *msg = *item;
    flux_msg_decref (msg);
}

/* zhashx_destructor_fn */

struct kvsroot *kvsroot_mgr_create_root (kvsroot_mgr_t *krm,
                                         struct cache *cache,
                                         const char *hash_name,
                                         const char *ns,
                                         uint32_t owner,
                                         int flags)
{
    struct kvsroot *root;

    /* Don't modify hash while iterating */
    if (krm->iterating_roots) {
        errno = EAGAIN;
        return NULL;
    }

    if (!(root = calloc (1, sizeof (*root)))) {
        flux_log_error (krm->h, "calloc");
        return NULL;
    }

    if (!(root->ns_name = strdup (ns))) {
        flux_log_error (krm->h, "strdup");
        goto error;
    }

    if (streq (root->ns_name, KVS_PRIMARY_NAMESPACE))
        root->is_primary = true;

    if (!(root->ktm = kvstxn_mgr_create (cache,
                                         root->ns_name,
                                         hash_name,
                                         krm->h,
                                         krm->arg))) {
        flux_log_error (krm->h, "kvstxn_mgr_create");
        goto error;
    }

    if (!(root->transaction_requests = zhashx_new ())) {
        flux_log_error (krm->h, "zhashx_new");
        goto error;
    }

    zhashx_set_duplicator (root->transaction_requests,
                           (zhashx_duplicator_fn *)flux_msg_incref);

    zhashx_set_destructor (root->transaction_requests,
                           (zhashx_destructor_fn *)flux_msg_decref_wrapper);

    if (!(root->wait_version_list = zlistx_new ())) {
        flux_log_error (krm->h, "zlistx_new");
        goto error;
    }

    zlistx_set_destructor (root->wait_version_list,
                           (zlistx_destructor_fn *)kvs_wait_version_destroy);

    zlistx_set_comparator (root->wait_version_list,
                           (zlistx_comparator_fn *)kvs_wait_version_cmp);

    root->owner = owner;
    root->flags = flags;
    root->remove = false;

    if (zhashx_insert (krm->roothash, ns, root) < 0) {
        errno = EEXIST;
        flux_log_error (krm->h, "zhashx_insert");
        goto error;
    }

    list_node_init (&root->work_queue_node);
    return root;

 error:
    kvsroot_destroy ((void **)&root);
    return NULL;
}

int kvsroot_mgr_remove_root (kvsroot_mgr_t *krm, const char *ns)
{
    /* don't want to remove while iterating, so save namespace for
     * later removal */
    if (krm->iterating_roots) {
        if (zlistx_add_end (krm->removelist, (void *)ns) < 0) {
            errno = ENOMEM;
            return -1;
        }
    }
    else
        zhashx_delete (krm->roothash, ns);
    return 0;
}

struct kvsroot *kvsroot_mgr_lookup_root (kvsroot_mgr_t *krm,
                                         const char *ns)
{
    return zhashx_lookup (krm->roothash, ns);
}

struct kvsroot *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *krm,
                                              const char *ns)
{
    struct kvsroot *root;

    if ((root = kvsroot_mgr_lookup_root (krm, ns))) {
        if (root->remove)
            root = NULL;
    }
    return root;
}

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *krm, kvsroot_root_f cb, void *arg)
{
    struct kvsroot *root;
    char *ns;

    krm->iterating_roots = true;

    root = zhashx_first (krm->roothash);
    while (root) {
        int ret;

        if ((ret = cb (root, arg)) < 0)
            goto error;

        if (ret == 1)
            break;

        root = zhashx_next (krm->roothash);
    }

    krm->iterating_roots = false;

    zlistx_head (krm->removelist);
    while ((ns = zlistx_detach_cur (krm->removelist))) {
        kvsroot_mgr_remove_root (krm, ns);
        free (ns);
    }

    return 0;

error:
    zlistx_purge (krm->removelist);
    krm->iterating_roots = false;
    return -1;
}

/* Convenience functions on struct kvsroot
 */

int kvsroot_save_transaction_request (struct kvsroot *root,
                                      const flux_msg_t *request,
                                      const char *name)
{
    if (!root || !request) {
        errno = EINVAL;
        return -1;
    }

    if (zhashx_insert (root->transaction_requests,
                       name,
                       (void *)request) < 0) {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

void kvsroot_setroot (kvsroot_mgr_t *krm,
                      struct kvsroot *root,
                      const char *root_ref,
                      int root_seq)
{
    if (!root || !root_ref)
        return;

    assert (strlen (root_ref) < sizeof (root->ref));

    strcpy (root->ref, root_ref);
    root->seq = root_seq;
}

int kvsroot_check_user (kvsroot_mgr_t *krm,
                        struct kvsroot *root,
                        struct flux_msg_cred cred)
{
    if (!root) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_cred_authorize (cred, root->owner) < 0)
        return -1;
    return 0;
}

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

static void kvs_wait_version_destroy (void **data)
{
    if (data) {
        struct kvs_wait_version *ks = *data;
        int save_errno = errno;
        flux_msg_decref (ks->msg);
        free (ks);
        errno = save_errno;
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
        return -1;
    }

    if (!(kwv = calloc (1, sizeof (*kwv))))
        return -1;

    kwv->msg = flux_msg_incref (msg);
    kwv->cb = cb;
    kwv->h = h;
    kwv->mh = mh;
    kwv->arg = arg;
    kwv->seq = seq;

    if (!zlistx_add_start (root->wait_version_list, kwv)) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_sort (root->wait_version_list);

    return 0;

 error:
    kvs_wait_version_destroy ((void **)&kwv);
    return -1;
}

void kvs_wait_version_process (struct kvsroot *root, bool all)
{
    struct kvs_wait_version *kwv;

    if (!root)
        return;

    /* notify sync waiters that version has been reached */

    kwv = zlistx_first (root->wait_version_list);
    while (kwv && (all || root->seq >= kwv->seq)) {
        kwv = zlistx_detach_cur (root->wait_version_list);
        kwv->cb (kwv->h, kwv->mh, kwv->msg, kwv->arg);
        kvs_wait_version_destroy ((void **)&kwv);
        kwv = zlistx_first (root->wait_version_list);
    }
}

int kvs_wait_version_remove_msg (struct kvsroot *root,
                                 kvs_wait_version_test_msg_f cmp,
                                 void *arg)
{
    struct kvs_wait_version *kwv;

    if (!root || !cmp) {
        errno = EINVAL;
        return -1;
    }

    kwv = zlistx_first (root->wait_version_list);
    while (kwv) {
        if (cmp (kwv->msg, arg)) {
            zlistx_detach_cur (root->wait_version_list);
            kvs_wait_version_destroy ((void **)&kwv);
        }
        kwv = zlistx_next (root->wait_version_list);
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
