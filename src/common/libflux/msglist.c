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
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <sys/eventfd.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "msglist.h"

struct flux_msglist {
    zlistx_t *zl;
    int pollevents;
    int pollfd;
    uint64_t event;
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
    l->pollfd = -1;
    l->pollevents = POLLOUT;
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
        if (l->pollfd >= 0)
            close (l->pollfd);
        free (l);
        errno = saved_errno;
    }
}

static int msglist_raise_event (struct flux_msglist *l)
{
    if (l->pollfd >= 0 && l->event == 0) {
        l->event = 1;
    if (write (l->pollfd, &l->event, sizeof (l->event)) < 0)
        return -1;
    }
    return 0;
}

static int msglist_clear_event (struct flux_msglist *l)
{
    if (l->pollfd >= 0 && l->event == 1) {
        if (read (l->pollfd, &l->event, sizeof (l->event)) < 0) {
            if (errno != EAGAIN  && errno != EWOULDBLOCK)
                return -1;
            errno = 0;
        }
        l->event = 0;
    }
    return 0;
}

int flux_msglist_append (struct flux_msglist *l, const flux_msg_t *msg)
{
    if (!(l->pollevents & POLLIN)) {
        l->pollevents |= POLLIN;
        if (msglist_raise_event (l) < 0)
            return -1;
    }
    if (!zlistx_add_end (l->zl, (flux_msg_t *)msg)) {
        l->pollevents |= POLLERR;
        msglist_raise_event (l);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int flux_msglist_push (struct flux_msglist *l, const flux_msg_t *msg)
{
    if (!(l->pollevents & POLLIN)) {
        l->pollevents |= POLLIN;
        if (msglist_raise_event (l) < 0)
            return -1;
    }
    if (!zlistx_add_start (l->zl, (flux_msg_t *)msg)) {
        l->pollevents |= POLLERR;
        msglist_raise_event (l);
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
    if (handle) {
        zlistx_delete (l->zl, handle);
        if ((l->pollevents & POLLIN) && zlistx_size (l->zl) == 0)
            l->pollevents &= ~POLLIN;
    }
}

const flux_msg_t *flux_msglist_pop (struct flux_msglist *l)
{
    void *item = zlistx_detach_cur (l->zl);
    if (item) {
        if ((l->pollevents & POLLIN) && zlistx_size (l->zl) == 0)
            l->pollevents &= ~POLLIN;
    }
    return item;
}

int flux_msglist_count (struct flux_msglist *l)
{
    return l ? zlistx_size (l->zl) : 0;
}

int flux_msglist_pollfd (struct flux_msglist *l)
{
    if (l->pollfd < 0) {
        l->event = l->pollevents ? 1 : 0;
        l->pollfd = eventfd (l->pollevents, EFD_NONBLOCK);
    }
    return l->pollfd;
}

int flux_msglist_pollevents (struct flux_msglist *l)
{
    if (msglist_clear_event (l) < 0)
        return -1;
    return l->pollevents;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

