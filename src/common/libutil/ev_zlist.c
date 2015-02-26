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

#include <czmq.h>

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zlist.h"

static void prepare_cb (struct ev_loop *loop, ev_prepare *w, int revents)
{
    ev_zlist *zw = (ev_zlist *)((char *)w - offsetof (ev_zlist, prepare_w));

    revents = 0;
    if ((zw->events & EV_READ) && zlist_size (zw->zlist) > 0)
        revents |= EV_READ;
    if ((zw->events & EV_WRITE))
        revents |= EV_WRITE;
    if (revents)
        ev_idle_start (loop, &zw->idle_w);
}

static void check_cb (struct ev_loop *loop, ev_check *w, int revents)
{
    ev_zlist *zw = (ev_zlist *)((char *)w - offsetof (ev_zlist, check_w));

    ev_idle_stop (loop, &zw->idle_w);

    revents = 0;
    if ((zw->events & EV_READ) && zlist_size (zw->zlist) > 0)
        revents |= EV_READ;
    if ((zw->events & EV_WRITE))
        revents |= EV_WRITE;
    if (revents)
        zw->cb (loop, zw, revents);
}

int ev_zlist_init (ev_zlist *w, ev_zlist_cb cb, zlist_t *zlist, int events)
{
    w->cb = cb;
    w->events = events;
    w->zlist = zlist;

    ev_prepare_init (&w->prepare_w, prepare_cb);
    ev_check_init (&w->check_w, check_cb);
    ev_idle_init (&w->idle_w, NULL);

    return 0;
}

void ev_zlist_start (struct ev_loop *loop, ev_zlist *w)
{
    ev_prepare_start (loop, &w->prepare_w);
    ev_check_start (loop, &w->check_w);
}

void ev_zlist_stop (struct ev_loop *loop, ev_zlist *w)
{
    ev_prepare_stop (loop, &w->prepare_w);
    ev_check_stop (loop, &w->check_w);
    ev_idle_stop (loop, &w->idle_w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

