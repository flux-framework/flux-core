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
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libflux/watcher_private.h"

#include "fbuf_watcher.h"
#include "fbuf.h"

/* Read buffer watcher
 */

struct rbwatcher {
    int refcnt;
    flux_watcher_t *fd_w;
    flux_watcher_t *prepare_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int fd;
    struct fbuf *fbuf;
    bool start;     /* flag, if user started reactor */
    bool eof_read;  /* flag, if EOF on stream seen */
    bool eof_sent;  /* flag, if EOF to user sent */
    bool line;      /* flag, if line buffered */
    void *data;
};

static int validate_fd_nonblock (int fd)
{
    int flags;
    if (fd < 0)
        goto inval;
    if ((flags = fd_get_flags (fd)) < 0)
        return -1;
    if (!(flags & O_NONBLOCK))
        goto inval;
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static bool data_to_read (struct rbwatcher *rbw, bool *is_eof)
{
    if (rbw->line) {
        if (fbuf_has_line (rbw->fbuf))
            return true;
        else {
            /* if no line, but the buffer is full, we have to flush */
            if (!fbuf_space (rbw->fbuf))
                return true;
            /* if eof read, no lines, but left over data non-line data,
             * this data should be flushed to the user */
            if (rbw->eof_read && fbuf_bytes (rbw->fbuf))
                return true;
        }
    }
    else {
        if (fbuf_bytes (rbw->fbuf) > 0)
            return true;
    }

    if (rbw->eof_read && !rbw->eof_sent && !fbuf_bytes (rbw->fbuf)) {
        if (is_eof)
            (*is_eof) = true;
        return true;
    }

    return false;
}

static void rbwatcher_start (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (!rbw->start) {
        flux_watcher_start (rbw->prepare_w);
        flux_watcher_start (rbw->check_w);
        if (fbuf_space (rbw->fbuf) > 0)
            flux_watcher_start (rbw->fd_w);
        /* else: buffer full, buffer_notify_cb will be called
         * to re-enable io reactor when space is available */
        rbw->start = true;
    }
}

static void rbwatcher_stop (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (rbw->start) {
        flux_watcher_stop (rbw->prepare_w);
        flux_watcher_stop (rbw->check_w);
        flux_watcher_stop (rbw->fd_w);
        flux_watcher_stop (rbw->idle_w);
        rbw->start = false;
    }
}

static void rbwatcher_destroy (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (rbw) {
        flux_watcher_destroy (rbw->prepare_w);
        flux_watcher_destroy (rbw->check_w);
        flux_watcher_destroy (rbw->fd_w);
        flux_watcher_destroy (rbw->idle_w);
        fbuf_destroy (rbw->fbuf);
    }
}

static bool rbwatcher_is_active (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    return flux_watcher_is_active (rbw->prepare_w);
}

static void rbwatcher_prepare_cb (flux_reactor_t *r,
                                  flux_watcher_t *prepare_w,
                                  int prepare_revents,
                                  void *arg)
{
    flux_watcher_t *w = arg;
    struct rbwatcher *rbw = watcher_get_data (w);

    if (data_to_read (rbw, NULL))
        flux_watcher_start (rbw->idle_w);
}

static void rbwatcher_check_cb (flux_reactor_t *r,
                                flux_watcher_t *check_w,
                                int check_revents,
                                void *arg)
{
    flux_watcher_t *w = arg;
    struct rbwatcher *rbw = watcher_get_data (w);
    bool is_eof = false;

    flux_watcher_stop (rbw->idle_w);

    if (data_to_read (rbw, &is_eof)) {
        watcher_call (w, FLUX_POLLIN);
        if (is_eof)
            rbw->eof_sent = true;
    }
}

static void rbwatcher_fd_cb (flux_reactor_t *r,
                             flux_watcher_t *fd_w,
                             int fd_revents,
                             void *arg)
{
    flux_watcher_t *w = arg;
    struct rbwatcher *rbw = watcher_get_data (w);

    if (fd_revents & FLUX_POLLIN) {
        int ret, space;

        if ((space = fbuf_space (rbw->fbuf)) < 0)
            return;

        if ((ret = fbuf_write_from_fd (rbw->fbuf, rbw->fd, space)) < 0)
            return;

        if (!ret) {
            fbuf_read_watcher_decref (w);
            (void)fbuf_readonly (rbw->fbuf);
            flux_watcher_stop (fd_w);
        }
        else if (ret == space) {
            /* buffer full, rbwatcher_notify_cb will be called
             * to re-enable io reactor when space is available */
            flux_watcher_stop (fd_w);
        }
    }
    else {
        watcher_call (w, fd_revents);
    }
}

static void rbwatcher_notify_cb (struct fbuf *fb, void *arg)
{
    struct rbwatcher *rbw = arg;

    /* space is available, start ev io watcher again, assuming watcher
     * is not stopped by user */
    if (rbw->start && fbuf_space (fb) > 0)
        flux_watcher_start (rbw->fd_w);
}

static struct flux_watcher_ops rbwatcher_ops = {
    .start = rbwatcher_start,
    .stop = rbwatcher_stop,
    .destroy = rbwatcher_destroy,
    .is_active = rbwatcher_is_active,
};

flux_watcher_t *fbuf_read_watcher_create (flux_reactor_t *r,
                                          int fd,
                                          int size,
                                          flux_watcher_f cb,
                                          int flags,
                                          void *arg)
{
    struct rbwatcher *rbw;
    flux_watcher_t *w;

    if (validate_fd_nonblock (fd) < 0)
        return NULL;
    if (!(w = watcher_create (r, sizeof (*rbw), &rbwatcher_ops, cb, arg)))
        goto error;
    rbw = watcher_get_data (w);
    rbw->fd = fd;
    rbw->refcnt = 1;
    if ((flags & FBUF_WATCHER_LINE_BUFFER))
        rbw->line = true;
    if (!(rbw->fbuf = fbuf_create (size))
        || !(rbw->prepare_w = flux_prepare_watcher_create (r,
                                                           rbwatcher_prepare_cb,
                                                           w))
        || !(rbw->check_w = flux_check_watcher_create (r,
                                                       rbwatcher_check_cb,
                                                       w))
        || !(rbw->idle_w = flux_idle_watcher_create (r, NULL, NULL))
        || !(rbw->fd_w = flux_fd_watcher_create (r,
                                                 fd,
                                                 FLUX_POLLIN,
                                                 rbwatcher_fd_cb,
                                                 w)))
        goto error;
    fbuf_set_notify (rbw->fbuf, rbwatcher_notify_cb, rbw);
    return w;
error:
    ERRNO_SAFE_WRAP (flux_watcher_destroy, w);
    return NULL;
}

static int validate_rbwatcher (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &rbwatcher_ops) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

struct fbuf *fbuf_read_watcher_get_buffer (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (validate_rbwatcher (w) < 0)
        return NULL;
    return rbw->fbuf;
}

const char *fbuf_read_watcher_get_data (flux_watcher_t *w, int *lenp)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (validate_rbwatcher (w) < 0)
        return NULL;
    if (rbw->line) {
        const char *data;
        if (!(data = fbuf_read_line (rbw->fbuf, lenp)))
            return NULL;
        if (*lenp > 0)
            return data;
        /* if no space, have to flush data out */
        if (!(*lenp) && !fbuf_space (rbw->fbuf))
            return fbuf_read (rbw->fbuf, -1, lenp);
     }
    /* Not line-buffered, or reading last bit of data which does
     * not contain a newline. Read any data:
     */
    return fbuf_read (rbw->fbuf, -1, lenp);
}

void fbuf_read_watcher_incref (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (validate_rbwatcher (w) < 0)
        return;
    rbw->refcnt++;
}

void fbuf_read_watcher_decref (flux_watcher_t *w)
{
    struct rbwatcher *rbw = watcher_get_data (w);

    if (validate_rbwatcher (w) < 0)
        return;
    if (--rbw->refcnt == 0)
        rbw->eof_read = true;
}

/* Write buffer watcher
 */

struct wbwatcher {
    flux_watcher_t *fd_w;
    int fd;
    struct fbuf *fbuf;
    bool start;         /* flag, if user started reactor */
    bool eof;           /* flag, eof written */
    bool closed;        /* flag, fd has been closed */
    int  close_errno;   /* errno from close */
    bool initial_space; /* flag, initial space notified */
};

static void wbwatcher_start (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    if (!wbw->start) {
        /* do not start fd watcher unless
         * - we have not sent initial space
         * - there is data to be written out
         * - notify EOF
         */
        if (!wbw->initial_space || fbuf_bytes (wbw->fbuf) || wbw->eof)
            flux_watcher_start (wbw->fd_w);
        wbw->start = true;
    }
}

static void wbwatcher_stop (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);
    if (wbw->start) {
        flux_watcher_stop (wbw->fd_w);
        wbw->start = false;
    }
}

static bool wbwatcher_is_active (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    return flux_watcher_is_active (wbw->fd_w);
}

static void wbwatcher_destroy (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    if (wbw) {
        flux_watcher_destroy (wbw->fd_w);
        fbuf_destroy (wbw->fbuf);
    }
}

static void wbwatcher_fd_cb (flux_reactor_t *r,
                             flux_watcher_t *fd_w,
                             int revents,
                             void *arg)
{
    flux_watcher_t *w = arg;
    struct wbwatcher *wbw = watcher_get_data (w);

    if (revents & FLUX_POLLOUT) {
        int ret;

        /* Send one time notification so user knows initial buffer size */
        if (!wbw->initial_space) {
            watcher_call (w, revents);
            wbw->initial_space = true;
        }

        if ((ret = fbuf_read_to_fd (wbw->fbuf, wbw->fd, -1)) < 0) {
            watcher_call (w, FLUX_POLLERR);
            return;
        }

        if (ret) {
            watcher_call (w, revents);
        }

        if (!fbuf_bytes (wbw->fbuf) && wbw->eof) {
            if (close (wbw->fd) < 0)
                wbw->close_errno = errno;
            wbw->fd = -1;
            wbw->closed = true;
            wbw->eof = false;
            watcher_call (w, revents);
        }

        if (!fbuf_bytes (wbw->fbuf) && !wbw->eof)
            flux_watcher_stop (wbw->fd_w);
    }
    else {
        watcher_call (w, revents);
    }
}

static void wbwatcher_notify_cb (struct fbuf *fb, void *arg)
{
    struct wbwatcher *wbw = arg;

    /* data is available, start ev io watcher assuming user has
     * started the watcher. */
    if (wbw->start && fbuf_bytes (fb) > 0)
        flux_watcher_start (wbw->fd_w);
}

static struct flux_watcher_ops wbwatcher_ops = {
    .start = wbwatcher_start,
    .stop = wbwatcher_stop,
    .destroy = wbwatcher_destroy,
    .is_active = wbwatcher_is_active,
};

flux_watcher_t *fbuf_write_watcher_create (flux_reactor_t *r,
                                           int fd,
                                           int size,
                                           flux_watcher_f cb,
                                           int flags,
                                           void *arg)
{
    struct wbwatcher *wbw;
    flux_watcher_t *w = NULL;

    if (validate_fd_nonblock (fd) < 0)
        return NULL;
    if (!(w = watcher_create (r, sizeof (*wbw), &wbwatcher_ops, cb, arg)))
        goto error;
    wbw = watcher_get_data (w);
    wbw->fd = fd;
    if (!(wbw->fbuf = fbuf_create (size))
        || !(wbw->fd_w = flux_fd_watcher_create (r,
                                                 fd,
                                                 FLUX_POLLOUT,
                                                 wbwatcher_fd_cb,
                                                 w)))
        goto error;
    fbuf_set_notify (wbw->fbuf, wbwatcher_notify_cb, wbw);
    return w;

error:
    flux_watcher_destroy (w);
    return NULL;
}

static int validate_wbwatcher (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &wbwatcher_ops) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

struct fbuf *fbuf_write_watcher_get_buffer (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    if (validate_wbwatcher (w) < 0)
        return NULL;
    return wbw->fbuf;
}

int fbuf_write_watcher_close (flux_watcher_t *w)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    if (validate_wbwatcher (w) < 0)
        return -1;
    if (wbw->eof) {
        errno = EINPROGRESS;
        return -1;
    }
    if (wbw->closed) {
        errno = EINVAL;
        return -1;
    }
    wbw->eof = true;
    fbuf_readonly (wbw->fbuf);
    if (wbw->start)
        flux_watcher_start (wbw->fd_w);
    return 0;
}

int fbuf_write_watcher_is_closed (flux_watcher_t *w, int *errp)
{
    struct wbwatcher *wbw = watcher_get_data (w);

    if (validate_wbwatcher (w) < 0)
        return 0;
    if (wbw->closed && errp != NULL)
        *errp = wbw->close_errno;
    return wbw->closed;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
