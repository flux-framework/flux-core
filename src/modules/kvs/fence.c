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

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "fence.h"

void fence_destroy (fence_t *f)
{
    if (f) {
        Jput (f->names);
        Jput (f->ops);
        if (f->requests) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (f->requests)))
                flux_msg_destroy (msg);
            /* FIXME: respond with error here? */
            zlist_destroy (&f->requests);
        }
        free (f);
    }
}

fence_t *fence_create (const char *name, int nprocs, int flags)
{
    fence_t *f;

    if (!(f = calloc (1, sizeof (*f))) || !(f->ops = json_object_new_array ())
                                       || !(f->requests = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    f->nprocs = nprocs;
    f->flags = flags;
    f->names = Jnew_ar ();
    Jadd_ar_str (f->names, name);

    return f;
error:
    fence_destroy (f);
    return NULL;
}

int fence_add_request_data (fence_t *f, json_object *ops)
{
    json_object *op;
    int i;

    if (ops) {
        for (i = 0; i < json_object_array_length (ops); i++) {
            if ((op = json_object_array_get_idx (ops, i)))
                if (json_object_array_add (f->ops, Jget (op)) < 0) {
                    Jput (op);
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
    return 0;
}

int fence_merge (fence_t *dest, fence_t *src)
{
    int i, len;

    if (dest->flags & KVS_NO_MERGE
        || src->flags & KVS_NO_MERGE)
        return 0;

    if (Jget_ar_len (src->names, &len)) {
        for (i = 0; i < len; i++) {
            const char *name;
            if (Jget_ar_str (src->names, i, &name))
                Jadd_ar_str (dest->names, name);
        }
    }
    if (Jget_ar_len (src->ops, &len)) {
        for (i = 0; i < len; i++) {
            json_object *op;
            if (Jget_ar_obj (src->ops, i, &op))
                Jadd_ar_obj (dest->ops, op);
        }
    }
    return 1;
}
