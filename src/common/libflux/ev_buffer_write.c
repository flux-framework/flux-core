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

#include <stddef.h>
#include <stdbool.h>

#include "src/common/libev/ev.h"

#include "ev_buffer_write.h"

#include "buffer_private.h"

static void buffer_write_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct ev_buffer_write *ebw = iow->data;

    if (revents & EV_WRITE) {

        if (flux_buffer_read_to_fd (ebw->fb, ebw->fd, -1) < 0)
            return;

        if (!flux_buffer_bytes (ebw->fb))
            ev_io_stop (ebw->loop, &(ebw->io_w));
    }
    else {
        if (ebw->cb)
            ebw->cb (loop, ebw, revents);
    }
}

static void buffer_data_available_cb (flux_buffer_t *fb, void *arg)
{
    struct ev_buffer_write *ebw = arg;

    /* data is available, start ev io watcher assuming user has
     * started the watcher.
     */
    if (ebw->start)
        ev_io_start (ebw->loop, &(ebw->io_w));
}

int ev_buffer_write_init (struct ev_buffer_write *ebw,
                          int fd,
                          int size,
                          ev_buffer_write_f cb,
                          struct ev_loop *loop)
{
    ebw->cb = cb;
    ebw->fd = fd;
    ebw->loop = loop;
    ebw->start = false;

    if (!(ebw->fb = flux_buffer_create (size)))
        goto cleanup;

    /* When any data becomes available, call buffer_data_available_cb,
     * which will start io reactor
     */
    if (flux_buffer_set_low_read_cb (ebw->fb,
                                     buffer_data_available_cb,
                                     0,
                                     ebw) < 0)
        goto cleanup;

    ev_io_init (&ebw->io_w, buffer_write_cb, ebw->fd, EV_WRITE);
    ebw->io_w.data = ebw;

    return 0;

cleanup:
    ev_buffer_write_cleanup (ebw);
    return -1;
}

void ev_buffer_write_cleanup (struct ev_buffer_write *ebw)
{
    if (ebw) {
        flux_buffer_destroy (ebw->fb);
        ebw->fb = NULL;
    }
}

void ev_buffer_write_start (struct ev_loop *loop, struct ev_buffer_write *ebw)
{
    if (!ebw->start) {
        ebw->start = true;
        /* do not start io watcher unless there is data to be written out */
        if (flux_buffer_bytes (ebw->fb))
            ev_io_start (ebw->loop, &(ebw->io_w));
    }
}

void ev_buffer_write_stop (struct ev_loop *loop, struct ev_buffer_write *ebw)
{
    if (ebw->start) {
        ev_io_stop (loop, &ebw->io_w);
        ebw->start = false;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

