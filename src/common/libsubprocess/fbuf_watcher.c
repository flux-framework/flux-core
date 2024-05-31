/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <flux/core.h>

#include "src/common/libev/ev.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libflux/reactor_private.h"

#include "fbuf_watcher.h"
#include "ev_fbuf_read.h"
#include "ev_fbuf_write.h"
#include "fbuf.h"

static void buffer_read_start (flux_watcher_t *w)
{
    struct ev_fbuf_read *ebr = (struct ev_fbuf_read *)w->data;
    ev_fbuf_read_start (w->r->loop, ebr);
}

static void buffer_read_stop (flux_watcher_t *w)
{
    struct ev_fbuf_read *ebr = (struct ev_fbuf_read *)w->data;
    ev_fbuf_read_stop (w->r->loop, ebr);
}

static bool buffer_read_is_active (flux_watcher_t *w)
{
    return ev_fbuf_read_is_active (w->data);
}

static void buffer_read_destroy (flux_watcher_t *w)
{
    struct ev_fbuf_read *ebr = (struct ev_fbuf_read *)w->data;
    ev_fbuf_read_cleanup (ebr);
}

static void buffer_read_cb (struct ev_loop *loop,
                            struct ev_fbuf_read *ebr,
                            int revents)
{
    struct flux_watcher *w = ebr->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops buffer_read_watcher = {
    .start = buffer_read_start,
    .stop = buffer_read_stop,
    .destroy = buffer_read_destroy,
    .is_active = buffer_read_is_active,
};

flux_watcher_t *fbuf_read_watcher_create (flux_reactor_t *r,
                                          int fd,
                                          int size,
                                          flux_watcher_f cb,
                                          int flags,
                                          void *arg)
{
    struct ev_fbuf_read *ebr;
    flux_watcher_t *w = NULL;
    int fd_flags;

    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((fd_flags = fd_get_flags (fd)) < 0)
        return NULL;
    if (!(fd_flags & O_NONBLOCK)) {
        errno = EINVAL;
        return NULL;
    }

    if (!(w = watcher_create (r,
                              sizeof (*ebr),
                              &buffer_read_watcher,
                              cb,
                              arg)))
        goto cleanup;

    ebr = watcher_get_data (w);

    if (ev_fbuf_read_init (ebr,
                           fd,
                           size,
                           buffer_read_cb,
                           r->loop) < 0)
        goto cleanup;

    if (flags & FBUF_WATCHER_LINE_BUFFER)
        ebr->line = true;

    ebr->data = w;

    return w;

cleanup:
    flux_watcher_destroy (w);
    return NULL;
}

struct fbuf *fbuf_read_watcher_get_buffer (flux_watcher_t *w)
{
    if (w)
        return ((struct ev_fbuf_read *)(w->data))->fb;
    return NULL;
}

const char *fbuf_read_watcher_get_data (flux_watcher_t *w, int *lenp)
{
    if (w) {
        struct ev_fbuf_read *eb = w->data;
        const char *data;
        if (eb->line) {
            if (!(data = fbuf_read_line (eb->fb, lenp)))
                return NULL;
            if (*lenp > 0)
                return data;
            /* if no space, have to flush data out */
            if (!(*lenp) && !fbuf_space (eb->fb))
                return fbuf_read (eb->fb, -1, lenp);
        }
        /* Not line-buffered, or reading last bit of data which does
         * not contain a newline. Read any data:
         */
        return fbuf_read (eb->fb, -1, lenp);
    }
    errno = EINVAL;
    return NULL;
}

void fbuf_read_watcher_incref (flux_watcher_t *w)
{
    if (w)
        ev_fbuf_read_incref ((struct ev_fbuf_read *)w->data);
}

void fbuf_read_watcher_decref (flux_watcher_t *w)
{
    if (w)
        ev_fbuf_read_decref ((struct ev_fbuf_read *)w->data);
}

static void buffer_write_start (flux_watcher_t *w)
{
    struct ev_fbuf_write *ebw = (struct ev_fbuf_write *)w->data;
    ev_fbuf_write_start (w->r->loop, ebw);
}

static void buffer_write_stop (flux_watcher_t *w)
{
    struct ev_fbuf_write *ebw = (struct ev_fbuf_write *)w->data;
    ev_fbuf_write_stop (w->r->loop, ebw);
}

static bool buffer_write_is_active (flux_watcher_t *w)
{
    return ev_fbuf_write_is_active (w->data);
}

static void buffer_write_destroy (flux_watcher_t *w)
{
    struct ev_fbuf_write *ebw = (struct ev_fbuf_write *)w->data;
    ev_fbuf_write_cleanup (ebw);
}

static void buffer_write_cb (struct ev_loop *loop,
                             struct ev_fbuf_write *ebw,
                             int revents)
{
    struct flux_watcher *w = ebw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops buffer_write_watcher = {
    .start = buffer_write_start,
    .stop = buffer_write_stop,
    .destroy = buffer_write_destroy,
    .is_active = buffer_write_is_active,
};

flux_watcher_t *fbuf_write_watcher_create (flux_reactor_t *r,
                                           int fd,
                                           int size,
                                           flux_watcher_f cb,
                                           int flags,
                                           void *arg)
{
    struct ev_fbuf_write *ebw;
    flux_watcher_t *w = NULL;
    int fd_flags;

    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }

    if ((fd_flags = fd_get_flags (fd)) < 0)
        return NULL;
    if (!(fd_flags & O_NONBLOCK)) {
        errno = EINVAL;
        return NULL;
    }

    if (!(w = watcher_create (r,
                              sizeof (*ebw),
                              &buffer_write_watcher,
                              cb,
                              arg)))
        goto cleanup;

    ebw = watcher_get_data (w);

    if (ev_fbuf_write_init (ebw,
                            fd,
                            size,
                            buffer_write_cb,
                            r->loop) < 0)
        goto cleanup;

    ebw->data = w;

    return w;

cleanup:
    flux_watcher_destroy (w);
    return NULL;
}

struct fbuf *fbuf_write_watcher_get_buffer (flux_watcher_t *w)
{
    if (w)
        return ((struct ev_fbuf_write *)(w->data))->fb;
    return NULL;
}

int fbuf_write_watcher_close (flux_watcher_t *w)
{
    struct ev_fbuf_write *evw;
    if (!w) {
        errno = EINVAL;
        return (-1);
    }
    evw = w->data;
    if (evw->eof) {
        errno = EINPROGRESS;
        return (-1);
    }
    if (evw->closed) {
        errno = EINVAL;
        return (-1);
    }
    evw->eof = true;
    fbuf_readonly (evw->fb);
    ev_fbuf_write_wakeup (evw);
    return (0);
}

int fbuf_write_watcher_is_closed (flux_watcher_t *w, int *errp)
{
    if (w) {
        struct ev_fbuf_write *evw = w->data;
        if (evw->closed && errp != NULL)
            *errp = evw->close_errno;
        return (evw->closed);
    }
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
