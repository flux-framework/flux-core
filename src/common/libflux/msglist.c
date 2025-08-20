/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "msglist.h"

struct flux_msglist {
    zlistx_t *zl;
};

static void *msg_duplicator (const void *item)
{
    return (flux_msg_t *)flux_msg_incref (item);
}

static void msg_destructor (void **item)
{
    if (item) {
        flux_msg_decref (*item);
        *item = NULL;
    }
}

struct flux_msglist *flux_msglist_create (void)
{
    struct flux_msglist *l;

    if (!(l = calloc (1, sizeof (*l))))
        return NULL;
    if (!(l->zl = zlistx_new ())) {
        free (l);
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (l->zl, msg_destructor);
    zlistx_set_duplicator (l->zl, msg_duplicator);
    return l;
}

void flux_msglist_destroy (struct flux_msglist *l)
{
    if (l) {
        int saved_errno = errno;
        zlistx_destroy (&l->zl);
        free (l);
        errno = saved_errno;
    }
}

int flux_msglist_append (struct flux_msglist *l, const flux_msg_t *msg)
{
    if (!zlistx_add_end (l->zl, (flux_msg_t *)msg)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int flux_msglist_push (struct flux_msglist *l, const flux_msg_t *msg)
{
    if (!zlistx_add_start (l->zl, (flux_msg_t *)msg)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

const flux_msg_t *flux_msglist_first (struct flux_msglist *l)
{
    return zlistx_first (l->zl);
}

const flux_msg_t *flux_msglist_next (struct flux_msglist *l)
{
    return zlistx_next (l->zl);
}

const flux_msg_t *flux_msglist_last (struct flux_msglist *l)
{
    return zlistx_last (l->zl);
}

void flux_msglist_delete (struct flux_msglist *l)
{
    void *handle = zlistx_cursor (l->zl);
    if (handle)
        zlistx_delete (l->zl, handle);
}

const flux_msg_t *flux_msglist_pop (struct flux_msglist *l)
{
    return zlistx_detach_cur (l->zl);
}

int flux_msglist_count (struct flux_msglist *l)
{
    return l ? zlistx_size (l->zl) : 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

