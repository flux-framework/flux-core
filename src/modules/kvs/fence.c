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

#include "fence.h"

struct fence_mgr {
    zhash_t *fences;
    bool iterating_fences;
    zlist_t *removelist;
};

struct fence {
    char *name;
    int nprocs;
    int count;
    zlist_t *requests;
    json_t *ops;
    int flags;
    int aux_int;
};

/*
 * fence_mgr_t functions
 */

fence_mgr_t *fence_mgr_create (void)
{
    fence_mgr_t *fm = NULL;
    int saved_errno;

    if (!(fm = calloc (1, sizeof (*fm)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(fm->fences = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    fm->iterating_fences = false;
    if (!(fm->removelist = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    return fm;

 error:
    fence_mgr_destroy (fm);
    errno = saved_errno;
    return NULL;
}

void fence_mgr_destroy (fence_mgr_t *fm)
{
    if (fm) {
        if (fm->fences)
            zhash_destroy (&fm->fences);
        if (fm->removelist)
            zlist_destroy (&fm->removelist);
        free (fm);
    }
}

int fence_mgr_add_fence (fence_mgr_t *fm, fence_t *f)
{
    /* Don't modify hash while iterating */
    if (fm->iterating_fences) {
        errno = EAGAIN;
        goto error;
    }

    if (zhash_insert (fm->fences, f->name, f) < 0) {
        errno = EEXIST;
        goto error;
    }

    /* initial fence aux int to 0 */
    fence_set_aux_int (f, 0);
    zhash_freefn (fm->fences,
                  fence_get_name (f),
                  (zhash_free_fn *)fence_destroy);
    return 0;
 error:
    return -1;
}

fence_t *fence_mgr_lookup_fence (fence_mgr_t *fm, const char *name)
{
    return zhash_lookup (fm->fences, name);
}

int fence_mgr_iter_not_ready_fences (fence_mgr_t *fm, fence_itr_f cb,
                                     void *data)
{
    fence_t *f;
    char *name;

    fm->iterating_fences = true;

    f = zhash_first (fm->fences);
    while (f) {
        if (!fence_count_reached (f)) {
            if (cb (f, data) < 0)
                goto error;
        }

        f = zhash_next (fm->fences);
    }

    fm->iterating_fences = false;

    while ((name = zlist_pop (fm->removelist))) {
        fence_mgr_remove_fence (fm, name);
        free (name);
    }

    return 0;

 error:
    while ((name = zlist_pop (fm->removelist)))
        free (name);
    fm->iterating_fences = false;
    return -1;
}

int fence_mgr_remove_fence (fence_mgr_t *fm, const char *name)
{
    /* it's dangerous to remove if we're in the middle of an
     * interation, so save fence for removal later.
     */
    if (fm->iterating_fences) {
        char *str = strdup (name);

        if (!str) {
            errno = ENOMEM;
            return -1;
        }

        if (zlist_append (fm->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    }
    else
        zhash_delete (fm->fences, name);
    return 0;
}

int fence_mgr_fences_count (fence_mgr_t *fm)
{
    return zhash_size (fm->fences);
}

/*
 * fence_t functions
 */

void fence_destroy (fence_t *f)
{
    if (f) {
        free (f->name);
        json_decref (f->ops);
        zlist_destroy (&f->requests);
        free (f);
    }
}

fence_t *fence_create (const char *name, int nprocs, int flags)
{
    fence_t *f = NULL;
    int saved_errno;

    if (!name || nprocs <= 0) {
        saved_errno = EINVAL;
        goto error;
    }
    if (!(f = calloc (1, sizeof (*f)))
        || !(f->ops = json_array ())
        || !(f->requests = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(f->name = strdup (name))) {
        saved_errno = ENOMEM;
        goto error;
    }
    f->nprocs = nprocs;
    f->flags = flags;
    f->aux_int = 0;

    return f;
error:
    fence_destroy (f);
    errno = saved_errno;
    return NULL;
}

bool fence_count_reached (fence_t *f)
{
    assert (f->count <= f->nprocs);
    return (f->count == f->nprocs);
}

const char *fence_get_name (fence_t *f)
{
    return f->name;
}

int fence_get_nprocs (fence_t *f)
{
    return f->nprocs;
}

int fence_get_flags (fence_t *f)
{
    return f->flags;
}

json_t *fence_get_json_ops (fence_t *f)
{
    return f->ops;
}

int fence_add_request_ops (fence_t *f, json_t *ops)
{
    json_t *op;
    int i;

    if (f->count == f->nprocs) {
        errno = EOVERFLOW;
        return -1;
    }

    if (ops) {
        for (i = 0; i < json_array_size (ops); i++) {
            if ((op = json_array_get (ops, i)))
                if (json_array_append (f->ops, op) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
        }
    }
    f->count++;
    return 0;
}

int fence_add_request_copy (fence_t *f, const flux_msg_t *request)
{
    flux_msg_t *cpy = flux_msg_copy (request, false);
    if (!cpy)
        return -1;
    if (zlist_push (f->requests, cpy) < 0) {
        flux_msg_destroy (cpy);
        return -1;
    }
    zlist_freefn (f->requests, cpy, (zlist_free_fn *)flux_msg_destroy, false);
    return 0;
}

int fence_iter_request_copies (fence_t *f, fence_msg_cb cb, void *data)
{
    flux_msg_t *msg;

    msg = zlist_first (f->requests);
    while (msg) {
        if (cb (f, msg, data) < 0)
            return -1;
        msg = zlist_next (f->requests);
    }

    return 0;
}

int fence_get_aux_int (fence_t *f)
{
    return f->aux_int;
}

void fence_set_aux_int (fence_t *f, int n)
{
    f->aux_int = n;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
