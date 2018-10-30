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

#include <stddef.h>
#include <stdbool.h>

#include "src/common/libev/ev.h"

#include "ev_buffer_read.h"

#include "buffer_private.h"

static bool data_to_read (struct ev_buffer_read *ebr, bool *is_eof)
{
    if (ebr->line) {
        if (flux_buffer_lines (ebr->fb) > 0)
            return true;
        /* if eof read, no lines, but left over data non-line data,
         * this data should be flushed to the user */
        else if (ebr->eof_read
                 && flux_buffer_bytes (ebr->fb))
            return true;
    }
    else {
        if (flux_buffer_bytes (ebr->fb) > 0)
            return true;
    }

    if (ebr->eof_read
        && !ebr->eof_sent
        && !flux_buffer_bytes (ebr->fb)) {
        if (is_eof)
            (*is_eof) = true;
        return true;
    }

    return false;
}

static void buffer_space_available_cb (flux_buffer_t *fb, void *arg)
{
    struct ev_buffer_read *ebr = arg;

    /* space is available, start ev io watcher again, assuming watcher
     * is not stopped by user */
    if (ebr->start)
        ev_io_start (ebr->loop, &(ebr->io_w));

    /* clear this callback */
    if (flux_buffer_set_high_write_cb (ebr->fb,
                                       NULL,
                                       0,
                                       NULL) < 0)
        return;
}

static void prepare_cb (struct ev_loop *loop, ev_prepare *w, int revents)
{
    struct ev_buffer_read *ebr = (struct ev_buffer_read *)((char *)w
                            - offsetof (struct ev_buffer_read, prepare_w));

    if (data_to_read (ebr, NULL) == true)
        ev_idle_start (loop, &ebr->idle_w);
}

static void buffer_read_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct ev_buffer_read *ebr = iow->data;

    if (revents & EV_READ) {
        int ret, space;

        if ((space = flux_buffer_space (ebr->fb)) < 0)
            return;

        if ((ret = flux_buffer_write_from_fd (ebr->fb, ebr->fd, space)) < 0)
            return;

        if (!ret) {
            ebr->eof_read = true;
            ev_io_stop (ebr->loop, iow);
        }
        else if (ret == space) {
            /* buffer full, buffer_space_available_cb will be called
             * to re-enable io reactor when space is available */
            if (flux_buffer_set_high_write_cb (ebr->fb,
                                               buffer_space_available_cb,
                                               flux_buffer_size (ebr->fb),
                                               ebr) < 0)
                return;

            ev_io_stop (ebr->loop, iow);
        }
    }
    else {
        if (ebr->cb)
            ebr->cb (loop, ebr, revents);
    }
}

static void check_cb (struct ev_loop *loop, ev_check *w, int revents)
{
    struct ev_buffer_read *ebr = (struct ev_buffer_read *)((char *)w
                            - offsetof (struct ev_buffer_read, check_w));
    bool is_eof = false;

    ev_idle_stop (loop, &ebr->idle_w);

    if (data_to_read (ebr, &is_eof) == true) {
        if (ebr->cb)
            ebr->cb (loop, ebr, EV_READ);

        if (is_eof)
            ebr->eof_sent = true;
    }
}

int ev_buffer_read_init (struct ev_buffer_read *ebr,
                         int fd,
                         int size,
                         ev_buffer_read_f cb,
                         struct ev_loop *loop)
{
    ebr->cb = cb;
    ebr->fd = fd;
    ebr->loop = loop;
    ebr->start = false;
    ebr->eof_read = false;
    ebr->eof_sent = false;

    if (!(ebr->fb = flux_buffer_create (size)))
        goto cleanup;

    ev_prepare_init (&ebr->prepare_w, prepare_cb);
    ev_check_init (&ebr->check_w, check_cb);
    ev_idle_init (&ebr->idle_w, NULL);
    ev_io_init (&ebr->io_w, buffer_read_cb, ebr->fd, EV_READ);
    ebr->io_w.data = ebr;

    return 0;

cleanup:
    ev_buffer_read_cleanup (ebr);
    return -1;
}

void ev_buffer_read_cleanup (struct ev_buffer_read *ebr)
{
    if (ebr) {
        flux_buffer_destroy (ebr->fb);
        ebr->fb = NULL;
    }
}

void ev_buffer_read_start (struct ev_loop *loop, struct ev_buffer_read *ebr)
{
    if (!ebr->start) {
        ebr->start = true;
        ev_prepare_start (loop, &ebr->prepare_w);
        ev_check_start (loop, &ebr->check_w);

        if (flux_buffer_space (ebr->fb) > 0)
            ev_io_start (ebr->loop, &(ebr->io_w));
        else {
            /* buffer full, buffer_space_available_cb will be called
             * to re-enable io reactor when space is available */
            if (flux_buffer_set_high_write_cb (ebr->fb,
                                               buffer_space_available_cb,
                                               flux_buffer_size (ebr->fb),
                                               ebr) < 0)
                return;
        }
    }
}

void ev_buffer_read_stop (struct ev_loop *loop, struct ev_buffer_read *ebr)
{
    if (ebr->start) {
        ev_prepare_stop (loop, &ebr->prepare_w);
        ev_check_stop (loop, &ebr->check_w);
        ev_io_stop (loop, &ebr->io_w);
        ev_idle_stop (loop, &ebr->idle_w);
        ebr->start = false;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

