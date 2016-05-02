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
#include <fcntl.h>
#include <string.h>
#include <czmq.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/liblsd/cbuf.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"

#include "inbuf.h"

#define INBUF_SIG  2000

/* The prep/check/idle business is how we assert a level-triggered
 * read event when there is data in the cbuf to consume.  See the note
 * in src/common/libutil/ev_zmq.c for more explanation.
 *
 * The buffering strategy should not drop data or expand the cbuf.
 * The file descriptor watcher is temporarily stopped when the cbuf is full,
 * then started again when there is space, which makes it possible for
 * the reader to participate in flow control, ultimately blocking the
 * writer while a chain of readers catches up.
 *
 * Because the buffer size is fixed, line buffering is "best effort".
 * When a line exceeds the size of the buffer, the line will be returned
 * in buffer-size chunks, without dropping data.
 */

struct inbuf {
    int fd;
    flux_watcher_t *fd_w;
    int bufsize;
    int flags;
    cbuf_t cbuf;
    bool eof;
    int errnum;
    flux_watcher_t *prep_w;
    flux_watcher_t *check_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *inbuf_w;
};

static void inbuf_start (void *impl, flux_watcher_t *w)
{
    struct inbuf *inbuf = impl;
    flux_watcher_start (inbuf->fd_w);
    flux_watcher_start (inbuf->prep_w);
    flux_watcher_start (inbuf->check_w);
}

static void inbuf_stop (void *impl, flux_watcher_t *w)
{
    struct inbuf *inbuf = impl;
    flux_watcher_stop (inbuf->fd_w);
    flux_watcher_stop (inbuf->prep_w);
    flux_watcher_stop (inbuf->check_w);
    flux_watcher_stop (inbuf->idle_w);
}

static void inbuf_destroy (void *impl, flux_watcher_t *w)
{
    struct inbuf *inbuf = impl;
    if (inbuf) {
        flux_watcher_destroy (inbuf->fd_w);
        flux_watcher_destroy (inbuf->prep_w);
        flux_watcher_destroy (inbuf->check_w);
        flux_watcher_destroy (inbuf->idle_w);
        if (inbuf->cbuf)
            cbuf_destroy (inbuf->cbuf);
        free (inbuf);
    }
}

/* File descriptor is ready.
 * Read data into the cbuf.  Set eof and errnum flags.
 * Disable ourselves if there is no more space in the cbuf.
 * (Re-enable in prep if reader freed some up)
 */
void fd_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct inbuf *inbuf = arg;
    if ((revents & FLUX_POLLERR)) {
        inbuf->errnum = errno;
    }
    if ((revents & FLUX_POLLIN) && cbuf_free (inbuf->cbuf) > 0) {
        int rc = cbuf_write_from_fd (inbuf->cbuf, inbuf->fd, -1, NULL);
        if (rc < 0)
            inbuf->errnum = errno;
        else if (rc == 0)
            inbuf->eof = true;
        if (cbuf_free (inbuf->cbuf) == 0)
            flux_watcher_stop (inbuf->fd_w);
    }
}

/* We are about to block.
 * Enable the idle watcher iff there is data in the buffer or other events.
 */
void prep_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct inbuf *inbuf = arg;
    bool linebuf = (inbuf->flags & INBUF_LINE_BUFFERED);

    revents = 0;
    if (inbuf->errnum != 0)
        revents |= FLUX_POLLERR;
    if (linebuf && cbuf_lines_used (inbuf->cbuf) > 0)
        revents |= FLUX_POLLIN;
    else if (linebuf && cbuf_free (inbuf->cbuf) == 0)
        revents |= FLUX_POLLIN;
    else if (!linebuf && cbuf_used (inbuf->cbuf) > 0)
        revents |= FLUX_POLLIN;
    else if (inbuf->eof)
        revents |= FLUX_POLLIN;

    if (revents)
        flux_watcher_start (inbuf->idle_w);
    if (cbuf_free (inbuf->cbuf) > 0)
        flux_watcher_start (inbuf->fd_w);
}

/* Just unblocked.
 * If revents, invoke the callback.
 */
void check_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct inbuf *inbuf = arg;
    bool linebuf = (inbuf->flags & INBUF_LINE_BUFFERED);

    flux_watcher_stop (inbuf->idle_w);

    revents = 0;
    if (inbuf->errnum != 0)
        revents |= FLUX_POLLERR;
    if (linebuf && cbuf_lines_used (inbuf->cbuf) > 0)
        revents |= FLUX_POLLIN;
    else if (linebuf && cbuf_free (inbuf->cbuf) == 0)
        revents |= FLUX_POLLIN;
    else if (!linebuf && cbuf_used (inbuf->cbuf) > 0)
        revents |= FLUX_POLLIN;
    else if (inbuf->eof)
        revents |= FLUX_POLLIN;
    if (revents)
        flux_watcher_call (inbuf->inbuf_w, revents);
}

static int set_nonblocking (int fd)
{
    int flags;
    if ((flags = fcntl (fd, F_GETFL, 0)) < 0
              || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

flux_watcher_t *flux_inbuf_watcher_create (flux_reactor_t *r, int fd,
                                           int bufsize, int flags,
                                           flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = inbuf_start,
        .stop = inbuf_stop,
        .destroy = inbuf_destroy,
    };

    struct inbuf *inbuf = xzmalloc (sizeof (*inbuf));
    inbuf->inbuf_w = flux_watcher_create (r, inbuf, ops, INBUF_SIG, cb, arg);
    if (!inbuf->inbuf_w) {
        free (inbuf);
        return NULL;
    }
    inbuf->fd = fd;
    if (set_nonblocking (fd) < 0)
        goto fail;
    inbuf->flags = flags;
    inbuf->bufsize = bufsize;
    inbuf->fd_w = flux_fd_watcher_create (r, fd, FLUX_POLLIN, fd_cb, inbuf);
    inbuf->prep_w = flux_prepare_watcher_create (r, prep_cb, inbuf);
    inbuf->check_w = flux_check_watcher_create (r, check_cb, inbuf);
    inbuf->idle_w = flux_idle_watcher_create (r, NULL, inbuf);
    if (!inbuf->fd_w || !inbuf->prep_w || !inbuf->check_w || !inbuf->idle_w)
        goto fail;
    if (!(inbuf->cbuf = cbuf_create (bufsize, bufsize)))
        goto fail;
    if (cbuf_opt_set (inbuf->cbuf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) < 0)
        goto fail;
    return inbuf->inbuf_w;
fail:
    flux_watcher_destroy (inbuf->inbuf_w);
    return NULL;
}


int flux_inbuf_watcher_read (flux_watcher_t *w, void *buf, int len)
{
    if (flux_watcher_get_signature (w) != INBUF_SIG) {
        errno = EINVAL;
        return -1;
    }
    struct inbuf *inbuf = flux_watcher_get_impl (w);
    int rc;

    if (inbuf->errnum != 0) {
        errno = inbuf->errnum;
        return -1;
    }
    if ((inbuf->flags & INBUF_LINE_BUFFERED)) {
        rc = cbuf_read_line (inbuf->cbuf, buf, len, 1);
        if (rc == 0 && (inbuf->eof || cbuf_free (inbuf->cbuf) == 0)) {
            rc = cbuf_read (inbuf->cbuf, buf, len - 1);
            if (rc > 0)
                ((char *)buf)[rc] = '\0';
        }
    } else
        rc = cbuf_read (inbuf->cbuf, buf, len);
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
