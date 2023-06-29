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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>

#include "handle.h"
#include "reactor.h"
#include "ev_flux.h"
#include "ev_buffer_read.h"
#include "ev_buffer_write.h"
#include "buffer.h"
#include "buffer_private.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdutils.h"

#include "reactor_private.h"

static int valid_flags (int flags, int valid)
{
    if ((flags & ~valid)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void reactor_usecount_decr (flux_reactor_t *r)
{
    if (r && --r->usecount == 0) {
        int saved_errno = errno;
        if (r->loop) {
            if (ev_is_default_loop (r->loop))
                ev_default_destroy ();
            else
                ev_loop_destroy (r->loop);
        }
        free (r);
        errno = saved_errno;
    }
}

static void reactor_usecount_incr (flux_reactor_t *r)
{
    r->usecount++;
}

void flux_reactor_destroy (flux_reactor_t *r)
{
    reactor_usecount_decr (r);
}

flux_reactor_t *flux_reactor_create (int flags)
{
    flux_reactor_t *r;

    if (valid_flags (flags, FLUX_REACTOR_SIGCHLD) < 0)
        return NULL;
    if (!(r = calloc (1, sizeof (*r))))
        return NULL;
    if ((flags & FLUX_REACTOR_SIGCHLD))
        r->loop = ev_default_loop (EVFLAG_SIGNALFD);
    else
        r->loop = ev_loop_new (EVFLAG_NOSIGMASK);
    if (!r->loop) {
        errno = ENOMEM;
        flux_reactor_destroy (r);
        return NULL;
    }
    ev_set_userdata (r->loop, r);
    r->usecount = 1;
    return r;
}

int flux_set_reactor (flux_t *h, flux_reactor_t *r)
{
    if (flux_aux_get (h, "flux::reactor")) {
        errno = EEXIST;
        return -1;
    }
    if (flux_aux_set (h, "flux::reactor", r, NULL) < 0)
        return -1;
    return 0;
}

flux_reactor_t *flux_get_reactor (flux_t *h)
{
    flux_reactor_t *r = flux_aux_get (h, "flux::reactor");
    if (!r) {
        if ((r = flux_reactor_create (0))) {
            if (flux_aux_set (h, "flux::reactor", r,
                              (flux_free_f)flux_reactor_destroy) < 0) {
                flux_reactor_destroy (r);
                r = NULL;
            }
        }
    }
    return r;
}

int flux_reactor_run (flux_reactor_t *r, int flags)
{
    int ev_flags = 0;
    int count;

    if (valid_flags (flags, FLUX_REACTOR_NOWAIT | FLUX_REACTOR_ONCE) < 0)
        return -1;
    if (flags & FLUX_REACTOR_NOWAIT)
        ev_flags |= EVRUN_NOWAIT;
    if (flags & FLUX_REACTOR_ONCE)
        ev_flags |= EVRUN_ONCE;
    r->errflag = 0;
    count = ev_run (r->loop, ev_flags);
    return (r->errflag ? -1 : count);
}

double flux_reactor_time (void)
{
    return ev_time ();
}

double flux_reactor_now (flux_reactor_t *r)
{
    return ev_now (r->loop);
}

void flux_reactor_now_update (flux_reactor_t *r)
{
    return ev_now_update (r->loop);
}

void flux_reactor_stop (flux_reactor_t *r)
{
    r->errflag = 0;
    ev_break (r->loop, EVBREAK_ALL);
}

void flux_reactor_stop_error (flux_reactor_t *r)
{
    r->errflag = 1;
    ev_break (r->loop, EVBREAK_ALL);
}

void flux_reactor_active_incref (flux_reactor_t *r)
{
    if (r)
        ev_ref (r->loop);
}

void flux_reactor_active_decref (flux_reactor_t *r)
{
    if (r)
        ev_unref (r->loop);
}

/**
 ** Watchers
 **/

flux_watcher_t *flux_watcher_create (flux_reactor_t *r,
                                     size_t data_size,
                                     struct flux_watcher_ops *ops,
                                     flux_watcher_f fun, void *arg)
{
    struct flux_watcher *w = calloc (1, sizeof (*w) + data_size);
    if (!w)
        return NULL;
    w->r = r;
    w->ops = ops;
    w->data = w + 1;
    w->fn = fun;
    w->arg = arg;
    reactor_usecount_incr (r);
    return w;
}

void * flux_watcher_get_data (flux_watcher_t *w)
{
    if (w)
        return w->data;
    return NULL;
}

struct flux_watcher_ops * flux_watcher_get_ops (flux_watcher_t *w)
{
    if (w)
        return w->ops;
    return NULL;
}

void flux_watcher_start (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->start)
            w->ops->start (w);
    }
}

void flux_watcher_stop (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->stop)
            w->ops->stop (w);
    }
}

void flux_watcher_destroy (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->stop)
            w->ops->stop (w);
        if (w->ops->destroy)
            w->ops->destroy (w);
        if (w->r)
            reactor_usecount_decr (w->r);
        free (w);
    }
}

static void safe_stop_cb (struct ev_loop *loop, ev_prepare *pw, int revents)
{
    flux_watcher_stop ((flux_watcher_t *)pw->data);
    ev_prepare_stop (loop, pw);
    free (pw);
}

/* Stop a watcher in the next ev_prepare callback. To be used from periodics
 *  reschedule callback or other ev callbacks in which it is documented
 *  unsafe to modify the ev_loop or any watcher.
 */
static void watcher_stop_safe (flux_watcher_t *w)
{
    if (w) {
        ev_prepare *pw = calloc (1, sizeof (*pw));
        if (!pw) /* On ENOMEM, we just have to give up */
            return;
        ev_prepare_init (pw, safe_stop_cb);
        pw->data = w;
        ev_prepare_start (w->r->loop, pw);
    }
}



/* flux_t handle
 */

static void handle_start (flux_watcher_t *w)
{
    ev_flux_start (w->r->loop, (struct ev_flux *)w->data);
}

static void handle_stop (flux_watcher_t *w)
{
    ev_flux_stop (w->r->loop, (struct ev_flux *)w->data);
}

static void handle_cb (struct ev_loop *loop, struct ev_flux *fw, int revents)
{
    struct flux_watcher *w = fw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops handle_watcher = {
    .start = handle_start,
    .stop = handle_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t *h, int events,
                                            flux_watcher_f cb, void *arg)
{
    struct ev_flux *fw;
    flux_watcher_t *w;
    if (!(w = flux_watcher_create (r, sizeof (*fw), &handle_watcher, cb, arg)))
        return NULL;
    fw = flux_watcher_get_data (w);
    ev_flux_init (fw, handle_cb, h, events_to_libev (events) & ~EV_ERROR);
    fw->data = w;

    return w;
}

flux_t *flux_handle_watcher_get_flux (flux_watcher_t *w)
{
    assert (flux_watcher_get_ops (w) == &handle_watcher);
    struct ev_flux *fw = w->data;
    return fw->h;
}

/* file descriptors
 */

static void fd_start (flux_watcher_t *w)
{
    ev_io_start (w->r->loop, (ev_io *)w->data);
}

static void fd_stop (flux_watcher_t *w)
{
    ev_io_stop (w->r->loop, (ev_io *)w->data);
}

static void fd_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct flux_watcher *w = iow->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops fd_watcher = {
    .start = fd_start,
    .stop = fd_stop,
    .destroy = NULL
};

flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r, int fd, int events,
                                        flux_watcher_f cb, void *arg)
{
    ev_io *iow;
    flux_watcher_t *w;

    if (!(w = flux_watcher_create (r, sizeof (*iow), &fd_watcher, cb, arg)))
        return NULL;
    iow = flux_watcher_get_data (w);
    ev_io_init (iow, fd_cb, fd, events_to_libev (events) & ~EV_ERROR);
    iow->data = w;

    return w;
}

int flux_fd_watcher_get_fd (flux_watcher_t *w)
{
    assert (flux_watcher_get_ops (w) == &fd_watcher);
    ev_io *iow = w->data;
    return iow->fd;
}

/* buffer
 */

static void buffer_read_start (flux_watcher_t *w)
{
    struct ev_buffer_read *ebr = (struct ev_buffer_read *)w->data;
    ev_buffer_read_start (w->r->loop, ebr);
}

static void buffer_read_stop (flux_watcher_t *w)
{
    struct ev_buffer_read *ebr = (struct ev_buffer_read *)w->data;
    ev_buffer_read_stop (w->r->loop, ebr);
}

static void buffer_read_destroy (flux_watcher_t *w)
{
    struct ev_buffer_read *ebr = (struct ev_buffer_read *)w->data;
    ev_buffer_read_cleanup (ebr);
}

static void buffer_read_cb (struct ev_loop *loop,
                            struct ev_buffer_read *ebr,
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
};

flux_watcher_t *flux_buffer_read_watcher_create (flux_reactor_t *r, int fd,
                                                 int size, flux_watcher_f cb,
                                                 int flags, void *arg)
{
    struct ev_buffer_read *ebr;
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

    if (!(w = flux_watcher_create (r,
                                   sizeof (*ebr),
                                   &buffer_read_watcher,
                                   cb,
                                   arg)))
        goto cleanup;

    ebr = flux_watcher_get_data (w);

    if (ev_buffer_read_init (ebr,
                             fd,
                             size,
                             buffer_read_cb,
                             r->loop) < 0)
        goto cleanup;

    if (flags & FLUX_WATCHER_LINE_BUFFER)
        ebr->line = true;

    ebr->data = w;

    return w;

cleanup:
    flux_watcher_destroy (w);
    return NULL;
}

flux_buffer_t *flux_buffer_read_watcher_get_buffer (flux_watcher_t *w)
{
    if (w)
        return ((struct ev_buffer_read *)(w->data))->fb;
    return NULL;
}

const char *flux_buffer_read_watcher_get_data (flux_watcher_t *w, int *lenp)
{
    if (w) {
        struct ev_buffer_read *eb = w->data;
        const char *data;
        if (eb->line) {
            if (!(data = flux_buffer_read_line (eb->fb, lenp)))
                return NULL;
            if (*lenp > 0)
                return data;
        }
        /* Not line-buffered, or reading last bit of data which does
         * not contain a newline. Read any data:
         */
        return flux_buffer_read (eb->fb, -1, lenp);
    }
    errno = EINVAL;
    return NULL;
}

void flux_buffer_read_watcher_incref (flux_watcher_t *w)
{
    if (w)
        ev_buffer_read_incref ((struct ev_buffer_read *)w->data);
}

void flux_buffer_read_watcher_decref (flux_watcher_t *w)
{
    if (w)
        ev_buffer_read_decref ((struct ev_buffer_read *)w->data);
}

static void buffer_write_start (flux_watcher_t *w)
{
    struct ev_buffer_write *ebw = (struct ev_buffer_write *)w->data;
    ev_buffer_write_start (w->r->loop, ebw);
}

static void buffer_write_stop (flux_watcher_t *w)
{
    struct ev_buffer_write *ebw = (struct ev_buffer_write *)w->data;
    ev_buffer_write_stop (w->r->loop, ebw);
}

static void buffer_write_destroy (flux_watcher_t *w)
{
    struct ev_buffer_write *ebw = (struct ev_buffer_write *)w->data;
    ev_buffer_write_cleanup (ebw);
}

static void buffer_write_cb (struct ev_loop *loop,
                             struct ev_buffer_write *ebw,
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
};

flux_watcher_t *flux_buffer_write_watcher_create (flux_reactor_t *r, int fd,
                                                  int size, flux_watcher_f cb,
                                                  int flags, void *arg)
{
    struct ev_buffer_write *ebw;
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

    if (!(w = flux_watcher_create (r,
                                   sizeof (*ebw),
                                   &buffer_write_watcher,
                                   cb,
                                   arg)))
        goto cleanup;

    ebw = flux_watcher_get_data (w);

    if (ev_buffer_write_init (ebw,
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

flux_buffer_t *flux_buffer_write_watcher_get_buffer (flux_watcher_t *w)
{
    if (w)
        return ((struct ev_buffer_write *)(w->data))->fb;
    return NULL;
}

int flux_buffer_write_watcher_close (flux_watcher_t *w)
{
    struct ev_buffer_write *evw;
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
    flux_buffer_readonly (evw->fb);
    ev_buffer_write_wakeup (evw);
    return (0);
}

int flux_buffer_write_watcher_is_closed (flux_watcher_t *w, int *errp)
{
    if (w) {
        struct ev_buffer_write *evw = w->data;
        if (evw->closed && errp != NULL)
            *errp = evw->close_errno;
        return (evw->closed);
    }
    return (0);
}

/* Timer
 */

static void timer_start (flux_watcher_t *w)
{
    ev_timer_start (w->r->loop, (ev_timer *)w->data);
}

static void timer_stop (flux_watcher_t *w)
{
    ev_timer_stop (w->r->loop, (ev_timer *)w->data);
}

static void timer_cb (struct ev_loop *loop, ev_timer *tw, int revents)
{
    struct flux_watcher *w = tw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops timer_watcher = {
    .start = timer_start,
    .stop = timer_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                           double after, double repeat,
                                           flux_watcher_f cb, void *arg)
{
    ev_timer *tw;
    flux_watcher_t *w;
    if (after < 0 || repeat < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = flux_watcher_create (r, sizeof (*tw), &timer_watcher, cb, arg)))
        return NULL;
    tw = flux_watcher_get_data (w);
    ev_timer_init (tw, timer_cb, after, repeat);
    tw->data = w;

    return w;
}

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat)
{
    assert (flux_watcher_get_ops (w) == &timer_watcher);
    ev_timer *tw = w->data;
    ev_timer_set (tw, after, repeat);
}

void flux_timer_watcher_again (flux_watcher_t *w)
{
    assert (flux_watcher_get_ops (w) == &timer_watcher);
    ev_timer *tw = w->data;
    struct ev_loop *loop = w->r->loop;
    ev_timer_again (loop, tw);
}

/* Periodic
 */
struct f_periodic {
    struct flux_watcher *w;
    ev_periodic          evp;
    flux_reschedule_f    reschedule_cb;
};

static void periodic_start (flux_watcher_t *w)
{
    struct f_periodic *fp = w->data;
    ev_periodic_start (w->r->loop, &fp->evp);
}

static void periodic_stop (flux_watcher_t *w)
{
    struct f_periodic *fp = w->data;
    ev_periodic_stop (w->r->loop, &fp->evp);
}

static void periodic_cb (struct ev_loop *loop, ev_periodic *pw, int revents)
{
    struct f_periodic *fp = pw->data;
    struct flux_watcher *w = fp->w;
    if (w->fn)
        fp->w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static ev_tstamp periodic_reschedule_cb (ev_periodic *pw, ev_tstamp now)
{
    ev_tstamp rc;
    struct f_periodic *fp = pw->data;
    assert (fp->reschedule_cb != NULL);
    rc = (ev_tstamp) fp->reschedule_cb (fp->w, (double) now, fp->w->arg);
    if (rc < now) {
        /*  User reschedule cb returned time in the past. The watcher will
         *   be stopped, but not here (changing loop is not allowed in a
         *   libev reschedule cb. flux_watcher_stop_safe() will stop it in
         *   a prepare callback.
         *  Return time far in the future to ensure we aren't called again.
         */
        watcher_stop_safe (fp->w);
        return (now + 1e99);
    }
    return rc;
}

static struct flux_watcher_ops periodic_watcher = {
    .start = periodic_start,
    .stop = periodic_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_periodic_watcher_create (flux_reactor_t *r,
                                              double offset, double interval,
                                              flux_reschedule_f reschedule_cb,
                                              flux_watcher_f cb, void *arg)
{
    flux_watcher_t *w;
    struct f_periodic *fp;
    size_t size = sizeof (*fp);
    if (offset < 0 || interval < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = flux_watcher_create (r, size, &periodic_watcher, cb, arg)))
        return NULL;
    fp = flux_watcher_get_data (w);
    fp->evp.data = fp;
    fp->w = w;
    fp->reschedule_cb = reschedule_cb;

    ev_periodic_init (&fp->evp, periodic_cb, offset, interval,
                      reschedule_cb ? periodic_reschedule_cb : NULL);

    return w;
}

void flux_periodic_watcher_reset (flux_watcher_t *w,
                                  double next, double interval,
                                  flux_reschedule_f reschedule_cb)
{
    struct f_periodic *fp = w->data;
    struct ev_loop *loop = w->r->loop;
    assert (flux_watcher_get_ops (w) == &periodic_watcher);
    fp->reschedule_cb = reschedule_cb;
    ev_periodic_set (&fp->evp, next, interval,
                     reschedule_cb ? periodic_reschedule_cb : NULL);
    ev_periodic_again (loop, &fp->evp);
}

double flux_watcher_next_wakeup (flux_watcher_t *w)
{
    if (flux_watcher_get_ops (w) == &periodic_watcher) {
        struct f_periodic *fp = w->data;
        return ((double) ev_periodic_at (&fp->evp));
    }
    else if (flux_watcher_get_ops (w) == &timer_watcher) {
        ev_timer *tw = w->data;
        struct ev_loop *loop = w->r->loop;
        return ((double) (ev_now (loop) +  ev_timer_remaining (loop, tw)));
    }
    errno = EINVAL;
    return  (-1.);
}

/* Prepare
 */
static void prepare_start (flux_watcher_t *w)
{
    ev_prepare_start (w->r->loop, (ev_prepare *)w->data);
}

static void prepare_stop (flux_watcher_t *w)
{
    ev_prepare_stop (w->r->loop, (ev_prepare *)w->data);
}

static void prepare_cb (struct ev_loop *loop, ev_prepare *pw, int revents)
{
    struct flux_watcher *w = pw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops prepare_watcher = {
    .start = prepare_start,
    .stop = prepare_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb, void *arg)
{
    ev_prepare *pw;
    flux_watcher_t *w;

    if (!(w = flux_watcher_create (r, sizeof (*pw), &prepare_watcher, cb, arg)))
        return NULL;
    pw = flux_watcher_get_data (w);
    ev_prepare_init (pw, prepare_cb);
    pw->data = w;

    return w;
}

/* Check
 */

static void check_start (flux_watcher_t *w)
{
    ev_check_start (w->r->loop, (ev_check *)w->data);
}

static void check_stop (flux_watcher_t *w)
{
    ev_check_stop (w->r->loop, (ev_check *)w->data);
}

static void check_cb (struct ev_loop *loop, ev_check *cw, int revents)
{
    struct flux_watcher *w = cw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops check_watcher = {
    .start = check_start,
    .stop = check_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                           flux_watcher_f cb, void *arg)
{
    ev_check *cw;
    flux_watcher_t *w;

    if (!(w = flux_watcher_create (r, sizeof (*cw), &check_watcher, cb, arg)))
        return NULL;
    cw = flux_watcher_get_data (w);
    ev_check_init (cw, check_cb);
    cw->data = w;

    return w;
}

/* Idle
 */

static void idle_start (flux_watcher_t *w)
{
    ev_idle_start (w->r->loop, (ev_idle *)w->data);
}

static void idle_stop (flux_watcher_t *w)
{
    ev_idle_stop (w->r->loop, (ev_idle *)w->data);
}

static void idle_cb (struct ev_loop *loop, ev_idle *iw, int revents)
{
    struct flux_watcher *w = iw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops idle_watcher = {
    .start = idle_start,
    .stop = idle_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb, void *arg)
{
    ev_idle *iw;
    flux_watcher_t *w;

    if (!(w = flux_watcher_create (r, sizeof (*iw), &idle_watcher, cb, arg)))
        return NULL;
    iw = flux_watcher_get_data (w);
    ev_idle_init (iw, idle_cb);
    iw->data = w;

    return w;
}

/* Child
 */

static void child_start (flux_watcher_t *w)
{
    ev_child_start (w->r->loop, (ev_child *)w->data);
}

static void child_stop (flux_watcher_t *w)
{
    ev_child_stop (w->r->loop, (ev_child *)w->data);
}

static void child_cb (struct ev_loop *loop, ev_child *cw, int revents)
{
    struct flux_watcher *w = cw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops child_watcher = {
    .start = child_start,
    .stop = child_stop,
    .destroy = NULL,
};


flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                           int pid, bool trace,
                                           flux_watcher_f cb, void *arg)
{
    flux_watcher_t *w;
    ev_child *cw;

    if (!ev_is_default_loop (r->loop)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = flux_watcher_create (r, sizeof (*cw), &child_watcher, cb, arg)))
        return NULL;
    cw = flux_watcher_get_data (w);
    ev_child_init (cw, child_cb, pid, trace ? 1 : 0);
    cw->data = w;

    return w;
}

int flux_child_watcher_get_rpid (flux_watcher_t *w)
{
    if (flux_watcher_get_ops (w) != &child_watcher) {
        errno = EINVAL;
        return -1;
    }
    ev_child *cw = w->data;
    return cw->rpid;
}

int flux_child_watcher_get_rstatus (flux_watcher_t *w)
{
    if (flux_watcher_get_ops (w) != &child_watcher) {
        errno = EINVAL;
        return -1;
    }
    ev_child *cw = w->data;
    return cw->rstatus;
}

/* Signal
 */

static void signal_start (flux_watcher_t *w)
{
    ev_signal_start (w->r->loop, (ev_signal *)w->data);
}

static void signal_stop (flux_watcher_t *w)
{
    ev_signal_stop (w->r->loop, (ev_signal *)w->data);
}

static void signal_cb (struct ev_loop *loop, ev_signal *sw, int revents)
{
    struct flux_watcher *w = sw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops signal_watcher = {
    .start = signal_start,
    .stop = signal_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_signal_watcher_create (flux_reactor_t *r, int signum,
                                            flux_watcher_f cb, void *arg)
{
    flux_watcher_t *w;
    ev_signal *sw;

    if (!(w = flux_watcher_create (r, sizeof (*sw), &signal_watcher, cb, arg)))
        return NULL;
    sw = flux_watcher_get_data (w);
    ev_signal_init (sw, signal_cb, signum);
    sw->data = w;

    return w;
}

int flux_signal_watcher_get_signum (flux_watcher_t *w)
{
    if (flux_watcher_get_ops (w) != &signal_watcher) {
        errno = EINVAL;
        return (-1);
    }
    ev_signal *sw = w->data;
    return sw->signum;
}

/* Stat
 */

static void stat_start (flux_watcher_t *w)
{
    ev_stat_start (w->r->loop, (ev_stat *)w->data);
}

static void stat_stop (flux_watcher_t *w)
{
    ev_stat_stop (w->r->loop, (ev_stat *)w->data);
}

static void stat_cb (struct ev_loop *loop, ev_stat *sw, int revents)
{
    struct flux_watcher *w = sw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops stat_watcher = {
    .start = stat_start,
    .stop = stat_stop,
    .destroy = NULL,
};

flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                          const char *path, double interval,
                                          flux_watcher_f cb, void *arg)
{
    flux_watcher_t *w;
    ev_stat *sw;

    if (!(w = flux_watcher_create (r, sizeof (*sw), &stat_watcher, cb, arg)))
        return NULL;
    sw = flux_watcher_get_data (w);
    ev_stat_init (sw, stat_cb, path, interval);
    sw->data = w;

    return w;
}

void flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                  struct stat *stat, struct stat *prev)
{
    ev_stat *sw = w->data;
    assert (flux_watcher_get_ops (w) == &stat_watcher);
    if (stat)
        *stat = sw->attr;
    if (prev)
        *prev = sw->prev;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
