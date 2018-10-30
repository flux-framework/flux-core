/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <czmq.h>

#include "msglist.h"

struct msglist_struct {
    zlist_t *zl;
    msglist_free_f destructor;
    int pollevents;
    int pollfd;
    uint64_t event;
};

static int raise_event (msglist_t *l)
{
    if (l->pollfd >= 0 && l->event == 0) {
        l->event = 1;
        if (write (l->pollfd, &l->event, sizeof (l->event)) < 0)
            return -1;
    }
    return 0;
}

static int clear_event (msglist_t *l)
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

void msglist_destroy (msglist_t *l)
{
    if (l) {
        if (l->zl) {
            void *item;
            while ((item = zlist_pop (l->zl)))
                if (l->destructor)
                    l->destructor (item);
            zlist_destroy (&l->zl);
        }
        if (l->pollfd >= 0)
            close (l->pollfd);
        free (l);
    }
}

msglist_t *msglist_create (msglist_free_f fun)
{
    msglist_t *l;

    if (!(l = malloc (sizeof (*l)))) {
        errno = ENOMEM;
        goto error;
    }
    memset (l, 0, sizeof (*l));
    l->pollfd = -1;
    if (!(l->zl = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    l->pollevents = POLLOUT;
    l->destructor = fun;
    return l;
error:
    msglist_destroy (l);
    return NULL;
}

void *msglist_pop (msglist_t *l)
{
    void *item = zlist_pop (l->zl);

    if ((l->pollevents & POLLIN) && zlist_size (l->zl) == 0)
        l->pollevents &= ~POLLIN;
    return item;
}

int msglist_push (msglist_t *l, void *item)
{
    int rc = -1;

    if (!(l->pollevents & POLLIN)) {
        l->pollevents |= POLLIN;
        if (raise_event (l) < 0)
            goto done;
    }
    if (zlist_push (l->zl, item) < 0) {
        l->pollevents |= POLLERR;
        (void)raise_event (l);
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int msglist_append (msglist_t *l, void *item)
{
    int rc = -1;

    if (!(l->pollevents & POLLIN)) {
        l->pollevents |= POLLIN;
        if (raise_event (l) < 0)
            goto done;
    }
    if (zlist_append (l->zl, item) < 0) {
        l->pollevents |= POLLERR;
        (void)raise_event (l);
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void *msglist_first (msglist_t *l)
{
    return zlist_first (l->zl);
}

void *msglist_next (msglist_t *l)
{
    return zlist_next (l->zl);
}

void msglist_remove (msglist_t *l, void *item)
{
    zlist_remove (l->zl, item);
    if ((l->pollevents & POLLIN) && zlist_size (l->zl) == 0)
        l->pollevents &= ~POLLIN;
}

int msglist_count (msglist_t *l)
{
    return zlist_size (l->zl);
}

int msglist_pollfd (msglist_t *l)
{
    if (l->pollfd < 0) {
        l->event = l->pollevents ? 1 : 0;
        l->pollfd = eventfd (l->pollevents, EFD_NONBLOCK);
    }
    return l->pollfd;
}

int msglist_pollevents (msglist_t *l)
{
    if (clear_event (l) < 0)
        return -1;
    return l->pollevents;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

