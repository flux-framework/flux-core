/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/libev/ev.h"

#include "ev_buffer_write.h"

#include "buffer_private.h"

static void buffer_write_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct ev_buffer_write *ebw = iow->data;

    if (revents & EV_WRITE) {

        if (flux_buffer_read_to_fd (ebw->fb, ebw->fd, -1) < 0)
            return;

        if (!flux_buffer_bytes (ebw->fb) && ebw->eof) {
            if (close (ebw->fd) < 0)
                ebw->close_errno = errno;
            ebw->fd = -1;
            ebw->closed = true;
            ebw->eof = false;
            if (ebw->cb)
                ebw->cb (loop, ebw, revents);
        }

        if (!flux_buffer_bytes (ebw->fb) && !ebw->eof)
            ev_io_stop (ebw->loop, &(ebw->io_w));
    }
    else {
        if (ebw->cb)
            ebw->cb (loop, ebw, revents);
    }
}

/* data is available, start ev io watcher assuming user has
 * started the watcher.
 */
void ev_buffer_write_wakeup (struct ev_buffer_write *ebw)
{
    if (ebw->start)
        ev_io_start (ebw->loop, &(ebw->io_w));
}

static void buffer_data_available_cb (flux_buffer_t *fb, void *arg)
{
    struct ev_buffer_write *ebw = arg;
    ev_buffer_write_wakeup (ebw);
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
        /* do not start watcher unless there is data or EOF to be written out */
        if (flux_buffer_bytes (ebw->fb) || ebw->eof)
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

