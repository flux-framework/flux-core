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
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

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
        if (root->namespace)
            free (root->namespace);
        if (root->ktm)
            kvstxn_mgr_destroy (root->ktm);
        if (root->trm)
            treq_mgr_destroy (root->trm);
        if (root->watchlist)
            wait_queue_destroy (root->watchlist);
        if (root->setroot_queue)
            zlist_destroy (&root->setroot_queue);
        free (data);
    }
}

struct kvsroot *kvsroot_mgr_create_root (kvsroot_mgr_t *krm,
                                         struct cache *cache,
                                         const char *hash_name,
                                         const char *namespace,
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

    if (!(root->namespace = strdup (namespace))) {
        flux_log_error (krm->h, "strdup");
        goto error;
    }

    if (!(root->ktm = kvstxn_mgr_create (cache,
                                         root->namespace,
                                         hash_name,
                                         krm->h,
                                         krm->arg))) {
        flux_log_error (krm->h, "kvstxn_mgr_create");
        goto error;
    }

    if (!(root->trm = treq_mgr_create ())) {
        flux_log_error (krm->h, "treq_mgr_create");
        goto error;
    }

    if (!(root->watchlist = wait_queue_create ())) {
        flux_log_error (krm->h, "wait_queue_create");
        goto error;
    }

    root->owner = owner;
    root->flags = flags;
    root->remove = false;

    if (zhash_insert (krm->roothash, namespace, root) < 0) {
        flux_log_error (krm->h, "zhash_insert");
        goto error;
    }

    if (!zhash_freefn (krm->roothash, namespace, kvsroot_destroy)) {
        flux_log_error (krm->h, "zhash_freefn");
        save_errnum = errno;
        zhash_delete (krm->roothash, namespace);
        errno = save_errnum;
        goto error;
    }

    return root;

 error:
    save_errnum = errno;
    kvsroot_destroy (root);
    errno = save_errnum;
    return NULL;
}

int kvsroot_mgr_remove_root (kvsroot_mgr_t *krm, const char *namespace)
{
    /* don't want to remove while iterating, so save namespace for
     * later removal */
    if (krm->iterating_roots) {
        char *str = strdup (namespace);

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
        zhash_delete (krm->roothash, namespace);
    return 0;
}

struct kvsroot *kvsroot_mgr_lookup_root (kvsroot_mgr_t *krm,
                                         const char *namespace)
{
    return zhash_lookup (krm->roothash, namespace);
}

struct kvsroot *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *krm,
                                              const char *namespace)
{
    struct kvsroot *root;

    if ((root = kvsroot_mgr_lookup_root (krm, namespace))) {
        if (root->remove)
            root = NULL;
    }
    return root;
}

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *krm, kvsroot_root_f cb, void *arg)
{
    struct kvsroot *root;
    char *namespace;

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

    while ((namespace = zlist_pop (krm->removelist))) {
        kvsroot_mgr_remove_root (krm, namespace);
        free (namespace);
    }

    return 0;

error:
    while ((namespace = zlist_pop (krm->removelist)))
        free (namespace);
    krm->iterating_roots = false;
    return -1;
}

/* Convenience functions on struct kvsroot
 */

void kvsroot_setroot (kvsroot_mgr_t *krm, struct kvsroot *root,
                      const char *root_ref, int root_seq)
{
    if (!root || !root_ref)
        return;

    assert (strlen (root_ref) < sizeof (root->ref));

    strcpy (root->ref, root_ref);
    root->seq = root_seq;
}

int kvsroot_check_user (kvsroot_mgr_t *krm, struct kvsroot *root,
                        uint32_t rolemask, uint32_t userid)
{
    if (!root) {
        errno = EINVAL;
        return -1;
    }

    if (rolemask & FLUX_ROLE_OWNER)
        return 0;

    if (rolemask & FLUX_ROLE_USER) {
        if (userid == root->owner)
            return 0;
    }

    errno = EPERM;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
