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
    zhash_t *roothash;
    zlist_t *removelist;
    bool iterating_roots;
    flux_t *h;
    void *arg;
};

kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg)
{
    kvsroot_mgr_t *krm = NULL;
    int saved_errno;

    if (!(krm = calloc (1, sizeof (*krm)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(krm->roothash = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(krm->removelist = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    krm->iterating_roots = false;
    krm->h = h;
    krm->arg = arg;
    return krm;

 error:
    kvsroot_mgr_destroy (krm);
    errno = saved_errno;
    return NULL;
}

void kvsroot_mgr_destroy (kvsroot_mgr_t *krm)
{
    if (krm) {
        if (krm->roothash)
            zhash_destroy (&krm->roothash);
        if (krm->removelist)
            zlist_destroy (&krm->removelist);
        free (krm);
    }
}

int kvsroot_mgr_root_count (kvsroot_mgr_t *krm)
{
    return zhash_size (krm->roothash);
}

static void kvsroot_destroy (void *data)
{
    if (data) {
        struct kvsroot *root = data;
        if (root->ns_name)
            free (root->ns_name);
        if (root->ktm)
            kvstxn_mgr_destroy (root->ktm);
        if (root->transaction_requests)
            zhash_destroy (&(root->transaction_requests));
        if (root->wait_version_list)
            zlist_destroy (&root->wait_version_list);
        if (root->setroot_queue)
            flux_msglist_destroy (root->setroot_queue);
        free (data);
    }
}

struct kvsroot *kvsroot_mgr_create_root (kvsroot_mgr_t *krm,
                                         struct cache *cache,
                                         const char *hash_name,
                                         const char *ns,
                                         uint32_t owner,
                                         int flags)
{
    struct kvsroot *root;
    int save_errnum;

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

    if (!(root->transaction_requests = zhash_new ())) {
        flux_log_error (krm->h, "zhash_new");
        goto error;
    }

    if (!(root->wait_version_list = zlist_new ())) {
        flux_log_error (krm->h, "zlist_new");
        goto error;
    }

    root->owner = owner;
    root->flags = flags;
    root->remove = false;

    if (zhash_insert (krm->roothash, ns, root) < 0) {
        errno = EEXIST;
        flux_log_error (krm->h, "zhash_insert");
        goto error;
    }

    if (!zhash_freefn (krm->roothash, ns, kvsroot_destroy)) {
        flux_log_error (krm->h, "zhash_freefn");
        save_errnum = errno;
        zhash_delete (krm->roothash, ns);
        errno = save_errnum;
        goto error;
    }

    list_node_init (&root->work_queue_node);
    return root;

 error:
    save_errnum = errno;
    kvsroot_destroy (root);
    errno = save_errnum;
    return NULL;
}

int kvsroot_mgr_remove_root (kvsroot_mgr_t *krm, const char *ns)
{
    /* don't want to remove while iterating, so save namespace for
     * later removal */
    if (krm->iterating_roots) {
        char *str = strdup (ns);

        if (!str) {
            errno = ENOMEM;
            return -1;
        }

        if (zlist_append (krm->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    }
    else
        zhash_delete (krm->roothash, ns);
    return 0;
}

struct kvsroot *kvsroot_mgr_lookup_root (kvsroot_mgr_t *krm,
                                         const char *ns)
{
    return zhash_lookup (krm->roothash, ns);
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

    root = zhash_first (krm->roothash);
    while (root) {
        int ret;

        if ((ret = cb (root, arg)) < 0)
            goto error;

        if (ret == 1)
            break;

        root = zhash_next (krm->roothash);
    }

    krm->iterating_roots = false;

    while ((ns = zlist_pop (krm->removelist))) {
        kvsroot_mgr_remove_root (krm, ns);
        free (ns);
    }

    return 0;

error:
    while ((ns = zlist_pop (krm->removelist)))
        free (ns);
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

    if (zhash_insert (root->transaction_requests,
                      name,
                      (void *)flux_msg_incref (request)) < 0) {
        flux_msg_decref (request);
        errno = EEXIST;
        return -1;
    }

    zhash_freefn (root->transaction_requests,
                  name,
                  (zhash_free_fn *)flux_msg_decref);
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
