/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watcher_uv.c - wrapped libuv watchers
 *
 * Notes on transitioning from libev:
 * - handle destruction is asynchronous, see libuv_close_cb() below
 * - timer requests are limited to millisecond precision
 * - watcher priorities cannot be changed
 * - no periodic watchers
 *
 * See also: flux-framework/flux-core#6492
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <uv.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "reactor_private.h"
#include "watcher_private.h"

static inline int events_to_libuv (int events)
{
    int e = 0;
    if (events & FLUX_POLLIN)
        e |= UV_READABLE;
    if (events & FLUX_POLLOUT)
        e |= UV_WRITABLE;
    if (events & FLUX_POLLERR)
        e |= UV_DISCONNECT;
    return e;
}

static inline int libuv_to_events (int events)
{
    int e = 0;
    if (events & UV_READABLE)
        e |= FLUX_POLLIN;
    if (events & UV_WRITABLE)
        e |= FLUX_POLLOUT;
    if (events & UV_DISCONNECT)
        e |= FLUX_POLLERR;
    return e;
}

/* A libuv handle cannot be directly destroyed.  ops->destroy() calls uv_close(),
 * registering libuv_close_cb(), which calls free().  If the reactor is destroyed
 * before the callback can run, handle memory is leaked.
 */
static void libuv_close_cb (uv_handle_t *uvh)
{
    free (uvh);
}

/* Generic callbacks that assume the first member of 'struct TYPE_watcher' below
 * is a uv_TYPE_t pointer that inherits from uv_handle_t.
 */
struct libuv_watcher {
    uv_handle_t *uvh;
};

static void libuv_watcher_ref (flux_watcher_t *w)
{
    struct libuv_watcher *uvw = watcher_get_data (w);
    uv_ref (uvw->uvh);
}

static void libuv_watcher_unref (flux_watcher_t *w)
{
    struct libuv_watcher *uvw = watcher_get_data (w);
    uv_unref (uvw->uvh);
}

static bool libuv_watcher_is_active (flux_watcher_t *w)
{
    struct libuv_watcher *uvw = watcher_get_data (w);
    return uv_is_active (uvw->uvh);
}

static void libuv_watcher_destroy (flux_watcher_t *w)
{
    struct libuv_watcher *uvw = watcher_get_data (w);
    uv_close (uvw->uvh, libuv_close_cb);
}

/* file descriptors
 */

struct fd_watcher {
    uv_poll_t *uvh;
    int revents;
};

static void fd_watcher_cb (uv_poll_t *uvh, int status, int events)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, status < 0 ? FLUX_POLLERR : libuv_to_events (events));
}

static void fd_watcher_start (flux_watcher_t *w)
{
    struct fd_watcher *fdw = watcher_get_data (w);
    uv_poll_start (fdw->uvh, fdw->revents, fd_watcher_cb);
}

static void fd_watcher_stop (flux_watcher_t *w)
{
    struct fd_watcher *fdw = watcher_get_data (w);
    uv_poll_stop (fdw->uvh);
}

static struct flux_watcher_ops fd_watcher_ops = {
    .start = fd_watcher_start,
    .stop = fd_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r,
                                        int fd,
                                        int events,
                                        flux_watcher_f cb,
                                        void *arg)
{
    struct fd_watcher *fdw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*fdw), &fd_watcher_ops, cb, arg)))
        return NULL;
    fdw = watcher_get_data (w);
    fdw->revents = events_to_libuv (events); // for uv_poll_start ()
    if (!(fdw->uvh = calloc (1, sizeof (*fdw->uvh))))
        goto error;
    uv_poll_init (reactor_get_loop (r), fdw->uvh, fd);
    uv_handle_set_data ((uv_handle_t *)fdw->uvh, w); // for fd_watcher_cb ()
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

int flux_fd_watcher_get_fd (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &fd_watcher_ops) {
        errno = EINVAL;
        return -1;
    }
    struct fd_watcher *fdw = watcher_get_data (w);
    int uverr;
    int fd;
    if ((uverr = uv_fileno ((uv_handle_t *)fdw->uvh, &fd)) < 0) {
        errno = -uverr;
        return -1;
    }
    return fd;
}

/* Timer
 */

struct timer_watcher {
    uv_timer_t *uvh;
    uint64_t timeout;
    uint64_t repeat;
};

static void timer_watcher_cb (uv_timer_t *uvh)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, 0);
}

static void timer_watcher_start (flux_watcher_t *w)
{
    struct timer_watcher *tmw = watcher_get_data (w);
    uv_timer_start (tmw->uvh, timer_watcher_cb, tmw->timeout, tmw->repeat);
}

static void timer_watcher_stop (flux_watcher_t *w)
{
    struct timer_watcher *tmw = watcher_get_data (w);
    uv_timer_stop (tmw->uvh);
}

static struct flux_watcher_ops timer_watcher_ops = {
    .start = timer_watcher_start,
    .stop = timer_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                           double after,
                                           double repeat,
                                           flux_watcher_f cb,
                                           void *arg)
{
    struct timer_watcher *tmw;
    flux_watcher_t *w;
    if (after < 0 || repeat < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = watcher_create (r, sizeof (*tmw), &timer_watcher_ops, cb, arg)))
        return NULL;
    tmw = watcher_get_data (w);
    if (!(tmw->uvh = calloc (1, sizeof (*tmw->uvh))))
        goto error;
    uv_timer_init (reactor_get_loop (r), tmw->uvh);
    uv_handle_set_data ((uv_handle_t *)(tmw->uvh), w); // for tmwatcher_cb()
    tmw->timeout = 1000ULL * after;
    tmw->repeat = 1000ULL * repeat;
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat)
{
    if (watcher_get_ops (w) != &timer_watcher_ops)
        return;
    struct timer_watcher *tmw = watcher_get_data (w);
    tmw->timeout = 1000ULL * after;
    tmw->repeat = 1000ULL * repeat;
}

void flux_timer_watcher_again (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &timer_watcher_ops)
        return;
    struct timer_watcher *tmw = watcher_get_data (w);
    /* in future.c::then_context_set_timeout() we assume that 'again' can be
     * run on a timer that hasn't been started.  That was apparently allowed
     * by libev, but is not allowed by libev
     */
    if (uv_timer_again (tmw->uvh) == UV_EINVAL) {
        if (tmw->repeat > 0)
            flux_watcher_start (w);
    }
}

double flux_watcher_next_wakeup (flux_watcher_t *w)
{
    if (watcher_get_ops (w) == &timer_watcher_ops) {
        struct timer_watcher *tmw = watcher_get_data (w);
        flux_reactor_t *r = watcher_get_reactor (w);
        return flux_reactor_now (r) + (1E-3 * uv_timer_get_due_in (tmw->uvh));
    }
    errno = EINVAL;
    return -1;
}

/* Prepare
 */

struct prepare_watcher {
    uv_prepare_t *uvh;
};

static void prepare_watcher_cb (uv_prepare_t *uvh)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, 0);
}

static void prepare_watcher_start (flux_watcher_t *w)
{
    struct prepare_watcher *pw = watcher_get_data (w);
    uv_prepare_start (pw->uvh, prepare_watcher_cb);
}

static void prepare_watcher_stop (flux_watcher_t *w)
{
    struct prepare_watcher *pw = watcher_get_data (w);
    uv_prepare_stop (pw->uvh);
}

static struct flux_watcher_ops prepare_watcher_ops = {
    .start = prepare_watcher_start,
    .stop = prepare_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb,
                                             void *arg)
{
    struct prepare_watcher *pw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*pw), &prepare_watcher_ops, cb, arg)))
        return NULL;
    pw = watcher_get_data (w);
    if (!(pw->uvh = calloc (1, sizeof (*pw->uvh))))
        goto error;
    uv_prepare_init (reactor_get_loop (r), pw->uvh);
    uv_handle_set_data ((uv_handle_t *)pw->uvh, w); // for prepare_watcher_cb ()
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

/* Check
 */

struct check_watcher {
    uv_check_t *uvh;
};

static void check_watcher_cb (uv_check_t *uvh)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, 0);
}

static void check_watcher_start (flux_watcher_t *w)
{
    struct check_watcher *cw = watcher_get_data (w);
    uv_check_start (cw->uvh, check_watcher_cb);
}

static void check_watcher_stop (flux_watcher_t *w)
{
    struct check_watcher *cw = watcher_get_data (w);
    uv_check_stop (cw->uvh);
}

static struct flux_watcher_ops check_watcher_ops = {
    .start = check_watcher_start,
    .stop = check_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb,
                                             void *arg)
{
    struct check_watcher *cw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*cw), &check_watcher_ops, cb, arg)))
        return NULL;
    cw = watcher_get_data (w);
    if (!(cw->uvh = calloc (1, sizeof (*cw->uvh))))
        goto error;
    uv_check_init (reactor_get_loop (r), cw->uvh);
    uv_handle_set_data ((uv_handle_t *)cw->uvh, w); // for check_watcher_cb ()
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

/* Idle
 */

struct idle_watcher {
    uv_idle_t *uvh;
};

static void idle_watcher_cb (uv_idle_t *uvh)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, 0);
}

static void idle_watcher_start (flux_watcher_t *w)
{
    struct idle_watcher *iw = watcher_get_data (w);
    uv_idle_start (iw->uvh, idle_watcher_cb);
}

static void idle_watcher_stop (flux_watcher_t *w)
{
    struct idle_watcher *iw = watcher_get_data (w);
    uv_idle_stop (iw->uvh);
}

static struct flux_watcher_ops idle_watcher_ops = {
    .start = idle_watcher_start,
    .stop = idle_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb,
                                             void *arg)
{
    struct idle_watcher *iw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*iw), &idle_watcher_ops, cb, arg)))
        return NULL;
    iw = watcher_get_data (w);
    if (!(iw->uvh = calloc (1, sizeof (*iw->uvh))))
        goto error;
    uv_idle_init (reactor_get_loop (r), iw->uvh);
    uv_handle_set_data ((uv_handle_t *)iw->uvh, w); // for idle_watcher_cb ()
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

/* Signal
 */

struct signal_watcher {
    uv_signal_t *uvh;
    int signum;
};

static void signal_watcher_cb (uv_signal_t *uvh, int signum)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    watcher_call (w, 0);
}

static void signal_watcher_start (flux_watcher_t *w)
{
    struct signal_watcher *sw = watcher_get_data (w);
    uv_signal_start (sw->uvh, signal_watcher_cb, sw->signum);
}

static void signal_watcher_stop (flux_watcher_t *w)
{
    struct signal_watcher *sw = watcher_get_data (w);
    uv_signal_stop (sw->uvh);
}

static struct flux_watcher_ops signal_watcher_ops = {
    .start = signal_watcher_start,
    .stop = signal_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = libuv_watcher_destroy,
};

flux_watcher_t *flux_signal_watcher_create (flux_reactor_t *r,
                                            int signum,
                                            flux_watcher_f cb,
                                            void *arg)
{
    flux_watcher_t *w;
    struct signal_watcher *sw;

    if (!(w = watcher_create (r, sizeof (*sw), &signal_watcher_ops, cb, arg)))
        return NULL;
    sw = watcher_get_data (w);
    if (!(sw->uvh = calloc (1, sizeof (*sw->uvh))))
        goto error;
    sw->signum = signum; // for sigwatcher_start()
    uv_signal_init (reactor_get_loop (r), sw->uvh);
    uv_handle_set_data ((uv_handle_t *)sw->uvh, w); // for sigwatcher_cb()
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

int flux_signal_watcher_get_signum (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &signal_watcher_ops) {
        errno = EINVAL;
        return -1;
    }
    struct signal_watcher *sw = watcher_get_data (w);
    return sw->signum;
}

/* Stat
 */

struct stat_watcher {
    uv_fs_event_t *uvh;
    char *path;
    struct stat prev;
    struct stat stat;
};

static void stat_watcher_cb (uv_fs_event_t *uvh,
                             const char *filename,
                             int events,
                             int status)
{
    struct flux_watcher *w = uv_handle_get_data ((uv_handle_t *)uvh);
    struct stat_watcher *sw = watcher_get_data (w);
    sw->prev = sw->stat;
    if (stat (sw->path, &sw->stat) < 0)
        sw->stat.st_nlink = 0;
    watcher_call (w, 0);
}

static void stat_watcher_start (flux_watcher_t *w)
{
    struct stat_watcher *sw = watcher_get_data (w);
    uv_fs_event_start (sw->uvh,
                       stat_watcher_cb,
                       sw->path,
                       UV_FS_EVENT_WATCH_ENTRY);
}

static void stat_watcher_stop (flux_watcher_t *w)
{
    struct stat_watcher *sw = watcher_get_data (w);
    uv_fs_event_stop (sw->uvh);
}

static void stat_watcher_destroy (flux_watcher_t *w)
{
    struct stat_watcher *sw = watcher_get_data (w);
    uv_close ((uv_handle_t *)sw->uvh, libuv_close_cb);
    ERRNO_SAFE_WRAP (free, sw->path);
}

static struct flux_watcher_ops stat_watcher_ops = {
    .start = stat_watcher_start,
    .stop = stat_watcher_stop,
    .ref = libuv_watcher_ref,
    .unref = libuv_watcher_unref,
    .is_active = libuv_watcher_is_active,
    .destroy = stat_watcher_destroy,
};

flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                          const char *path,
                                          double interval,
                                          flux_watcher_f cb,
                                          void *arg)
{
    flux_watcher_t *w;
    struct stat_watcher *sw;

    if (!(w = watcher_create (r, sizeof (*sw), &stat_watcher_ops, cb, arg)))
        return NULL;
    sw = watcher_get_data (w);
    if (!(sw->uvh = calloc (1, sizeof (*sw->uvh))))
        goto error;
    uv_fs_event_init (reactor_get_loop (r), sw->uvh);
    if (stat (path, &sw->stat) < 0)
        sw->stat.st_nlink = 0;
    sw->prev = sw->stat;
    if (!(sw->path = strdup (path)))
        goto error;
    uv_handle_set_data ((uv_handle_t *)sw->uvh, w);
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}

int flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                 struct stat *stat,
                                 struct stat *prev)
{
    if (watcher_get_ops (w) != &stat_watcher_ops) {
        errno = EINVAL;
        return -1;
    }
    struct stat_watcher *sw = watcher_get_data (w);
    if (stat)
        *stat = sw->stat;
    if (prev)
        *prev = sw->prev;
    return 0;
}

// vi:ts=4 sw=4 expandtab
