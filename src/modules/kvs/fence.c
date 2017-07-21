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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/oom.h"

#include "fence.h"

struct fence {
    int nprocs;
    int count;
    zlist_t *requests;
    json_t *ops;
    json_t *names;
    int flags;
};

void fence_destroy (fence_t *f)
{
    if (f) {
        json_decref (f->names);
        json_decref (f->ops);
        zlist_destroy (&f->requests);
        free (f);
    }
}

fence_t *fence_create (const char *name, int nprocs, int flags)
{
    fence_t *f;
    json_t *s = NULL;

    if (!(f = calloc (1, sizeof (*f)))
        || !(f->ops = json_array ())
        || !(f->names = json_array ())
        || !(f->requests = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    f->nprocs = nprocs;
    f->flags = flags;
    if (name) {
        if (!(s = json_string (name))) {
            errno = ENOMEM;
            goto error;
        }
        if (json_array_append_new (f->names, s) < 0) {
            json_decref (s);
            errno = ENOMEM;
            goto error;
        }
    }

    return f;
error:
    fence_destroy (f);
    return NULL;
}

bool fence_count_reached (fence_t *f)
{
    return (f->count >= f->nprocs);
}

int fence_get_flags (fence_t *f)
{
    return f->flags;
}

void fence_set_flags (fence_t *f, int flags)
{
    f->flags = flags;
}

json_t *fence_get_json_ops (fence_t *f)
{
    return f->ops;
}

json_t *fence_get_json_names (fence_t *f)
{
    return f->names;
}

int fence_add_request_data (fence_t *f, json_t *ops)
{
    json_t *op;
    int i;

    if (ops) {
        for (i = 0; i < json_array_size (ops); i++) {
            if ((op = json_array_get (ops, i)))
                if (json_array_append (f->ops, op) < 0) {
                    json_decref (op);
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

int fence_merge (fence_t *dest, fence_t *src)
{
    int i, len;

    if ((dest->flags & FLUX_KVS_NO_MERGE) || (src->flags & FLUX_KVS_NO_MERGE))
        return 0;

    if ((len = json_array_size (src->names))) {
        for (i = 0; i < len; i++) {
            json_t *name;
            if ((name = json_array_get (src->names, i))) {
                if (json_array_append (dest->names, name) < 0)
                    oom ();
            }
        }
    }
    if ((len = json_array_size (src->ops))) {
        for (i = 0; i < len; i++) {
            json_t *op;
            if ((op = json_array_get (src->ops, i))) {
                if (json_array_append (dest->ops, op) < 0)
                    oom ();
            }
        }
    }
    return 1;
}
