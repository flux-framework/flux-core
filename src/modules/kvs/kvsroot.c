/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

static void kvsroot_destroy (void *data)
{
    if (data) {
        struct kvsroot *root = data;
        if (root->namespace)
            free (root->namespace);
        if (root->cm)
            commit_mgr_destroy (root->cm);
        if (root->watchlist)
            wait_queue_destroy (root->watchlist);
        free (data);
    }
}

void kvsroot_remove (zhash_t *roothash, const char *namespace)
{
    zhash_delete (roothash, namespace);
}

struct kvsroot *kvsroot_lookup (zhash_t *roothash, const char *namespace)
{
    return zhash_lookup (roothash, namespace);
}

struct kvsroot *kvsroot_lookup_safe (zhash_t *roothash, const char *namespace)
{
    struct kvsroot *root;

    if ((root = kvsroot_lookup (roothash, namespace))) {
        if (root->remove)
            root = NULL;
    }
    return root;
}

struct kvsroot *kvsroot_create (zhash_t *roothash,
                                struct cache *cache,
                                const char *hash_name,
                                const char *namespace,
                                int flags,
                                flux_t *h,
                                void *arg)
{
    struct kvsroot *root;
    int save_errnum;

    if (!(root = calloc (1, sizeof (*root)))) {
        flux_log_error (h, "calloc");
        return NULL;
    }

    if (!(root->namespace = strdup (namespace))) {
        flux_log_error (h, "strdup");
        goto error;
    }

    if (!(root->cm = commit_mgr_create (cache,
                                        root->namespace,
                                        hash_name,
                                        h,
                                        arg))) {
        flux_log_error (h, "commit_mgr_create");
        goto error;
    }

    if (!(root->watchlist = wait_queue_create ())) {
        flux_log_error (h, "wait_queue_create");
        goto error;
    }

    root->flags = flags;
    root->remove = false;

    if (zhash_insert (roothash, namespace, root) < 0) {
        flux_log_error (h, "zhash_insert");
        goto error;
    }

    if (!zhash_freefn (roothash, namespace, kvsroot_destroy)) {
        flux_log_error (h, "zhash_freefn");
        save_errnum = errno;
        zhash_delete (roothash, namespace);
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
