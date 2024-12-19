/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watcher_wrap.c - wrapped libev watchers */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ev.h>
#include <flux/core.h>

#include "reactor_private.h"
#include "watcher_private.h"

static inline int events_to_libev (int events)
{
    int e = 0;
    if (events & FLUX_POLLIN)
        e |= EV_READ;
    if (events & FLUX_POLLOUT)
        e |= EV_WRITE;
    if (events & FLUX_POLLERR)
        e |= EV_ERROR;
    return e;
}

static inline int libev_to_events (int events)
{
    int e = 0;
    if (events & EV_READ)
        e |= FLUX_POLLIN;
    if (events & EV_WRITE)
        e |= FLUX_POLLOUT;
    if (events & EV_ERROR)
        e |= FLUX_POLLERR;
    return e;
}

static void watcher_call_ev (flux_watcher_t *w, int revents)
{
    watcher_call (w, libev_to_events (revents));
}

static struct ev_loop *watcher_get_ev (flux_watcher_t *w)
{
    return reactor_get_loop (watcher_get_reactor (w));
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
        ev_prepare_start (watcher_get_ev (w), pw);
    }
}

/* file descriptors
 */

struct fd_watcher {
    ev_io evw;
};

static void fd_watcher_start (flux_watcher_t *w)
{
    struct fd_watcher *fdw = watcher_get_data (w);
    struct ev_loop *loop = watcher_get_ev (w);
    ev_io_start (loop, &fdw->evw);
}

static void fd_watcher_stop (flux_watcher_t *w)
{
    struct fd_watcher *fdw = watcher_get_data (w);
    struct ev_loop *loop = watcher_get_ev (w);
    ev_io_stop (loop, &fdw->evw);
}

static bool fd_watcher_is_active (flux_watcher_t *w)
{
    struct fd_watcher *fdw = watcher_get_data (w);
    return ev_is_active (&fdw->evw);
}

static void fd_watcher_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct flux_watcher *w = iow->data;
    watcher_call_ev (w, revents);
}


static struct flux_watcher_ops fd_watcher_ops = {
    .start = fd_watcher_start,
    .stop = fd_watcher_stop,
    .is_active = fd_watcher_is_active,
    .destroy = NULL,
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
    ev_io_init (&fdw->evw,
                fd_watcher_cb,
                fd,
                events_to_libev (events) & ~EV_ERROR);
    fdw->evw.data = w;

    return w;
}

int flux_fd_watcher_get_fd (flux_watcher_t *w)
{
    if (watcher_get_ops (w) == &fd_watcher_ops) {
        struct fd_watcher *fdw = watcher_get_data (w);
        return fdw->evw.fd;
    }
    errno = EINVAL;
    return -1;
}

/* Timer
 */

struct timer_watcher {
    struct ev_timer evw;
};

static void timer_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct timer_watcher *tmw = watcher_get_data (w);
    ev_timer_start (loop, &tmw->evw);
}

static void timer_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct timer_watcher *tmw = watcher_get_data (w);
    ev_timer_stop (loop, &tmw->evw);
}

static void timer_watcher_cb (struct ev_loop *loop, ev_timer *evw, int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static bool timer_watcher_is_active (flux_watcher_t *w)
{
    struct timer_watcher *tmw = watcher_get_data (w);
    return ev_is_active (&tmw->evw);
}

static struct flux_watcher_ops timer_watcher_ops = {
    .start = timer_watcher_start,
    .stop = timer_watcher_stop,
    .is_active = timer_watcher_is_active,
    .destroy = NULL,
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
    ev_timer_init (&tmw->evw, timer_watcher_cb, after, repeat);
    tmw->evw.data = w;

    return w;
}

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat)
{
    if (watcher_get_ops (w) == &timer_watcher_ops) {
        struct timer_watcher *tmw = watcher_get_data (w);
        ev_timer_set (&tmw->evw, after, repeat);
    }
}

void flux_timer_watcher_again (flux_watcher_t *w)
{
    if (watcher_get_ops (w) == &timer_watcher_ops) {
        struct timer_watcher *tmw = watcher_get_data (w);
        struct ev_loop *loop = watcher_get_ev (w);
        ev_timer_again (loop, &tmw->evw);
    }
}

/* Periodic
 */

struct periodic_watcher {
    struct flux_watcher *w;
    ev_periodic evw;
    flux_reschedule_f reschedule_cb;
};

static void periodic_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct periodic_watcher *pdw = watcher_get_data (w);
    ev_periodic_start (loop, &pdw->evw);
}

static void periodic_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct periodic_watcher *pdw = watcher_get_data (w);
    ev_periodic_stop (loop, &pdw->evw);
}

static bool periodic_watcher_is_active (flux_watcher_t *w)
{
    struct periodic_watcher *pdw = watcher_get_data (w);
    return ev_is_active (&pdw->evw);
}

static void periodic_watcher_cb (struct ev_loop *loop,
                                 ev_periodic *pw,
                                 int revents)
{
    struct periodic_watcher *pdw = pw->data;
    struct flux_watcher *w = pdw->w;
    watcher_call_ev (w, revents);
}

static ev_tstamp periodic_watcher_reschedule_cb (ev_periodic *pw,
                                                 ev_tstamp now)
{
    struct periodic_watcher *pdw = pw->data;
    struct flux_watcher *w = pdw->w;
    ev_tstamp rc;
    if (pdw->reschedule_cb == NULL)
        return 0;
    rc = (ev_tstamp)pdw->reschedule_cb (w, (double)now, watcher_get_arg (w));
    if (rc < now) {
        /*  User reschedule cb returned time in the past. The watcher will
         *   be stopped, but not here (changing loop is not allowed in a
         *   libev reschedule cb. flux_watcher_stop_safe() will stop it in
         *   a prepare callback.
         *  Return time far in the future to ensure we aren't called again.
         */
        watcher_stop_safe (w);
        return (now + 1e99);
    }
    return rc;
}

static struct flux_watcher_ops periodic_watcher_ops = {
    .start = periodic_watcher_start,
    .stop = periodic_watcher_stop,
    .is_active = periodic_watcher_is_active,
    .destroy = NULL,
};

flux_watcher_t *flux_periodic_watcher_create (flux_reactor_t *r,
                                              double offset,
                                              double interval,
                                              flux_reschedule_f reschedule_cb,
                                              flux_watcher_f cb,
                                              void *arg)
{
    flux_watcher_t *w;
    struct periodic_watcher *pdw;
    size_t size = sizeof (*pdw);
    if (offset < 0 || interval < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = watcher_create (r, size, &periodic_watcher_ops, cb, arg)))
        return NULL;
    pdw = watcher_get_data (w);
    pdw->evw.data = pdw;
    pdw->w = w;
    pdw->reschedule_cb = reschedule_cb;

    ev_periodic_init (&pdw->evw,
                      periodic_watcher_cb,
                      offset,
                      interval,
                      reschedule_cb ? periodic_watcher_reschedule_cb : NULL);

    return w;
}

void flux_periodic_watcher_reset (flux_watcher_t *w,
                                  double next,
                                  double interval,
                                  flux_reschedule_f reschedule_cb)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct periodic_watcher *pdw = watcher_get_data (w);
    if (watcher_get_ops (w) == &periodic_watcher_ops) {
        pdw->reschedule_cb = reschedule_cb;
        ev_periodic_set (&pdw->evw,
                         next,
                         interval,
                         reschedule_cb ? periodic_watcher_reschedule_cb : NULL);
        ev_periodic_again (loop, &pdw->evw);
    }
}

double flux_watcher_next_wakeup (flux_watcher_t *w)
{
    if (watcher_get_ops (w) == &periodic_watcher_ops) {
        struct periodic_watcher *pdw = watcher_get_data (w);
        return ((double) ev_periodic_at (&pdw->evw));
    }
    else if (watcher_get_ops (w) == &timer_watcher_ops) {
        ev_timer *tw = watcher_get_data (w);
        struct ev_loop *loop = watcher_get_ev (w);
        return ((double) (ev_now (loop) +  ev_timer_remaining (loop, tw)));
    }
    errno = EINVAL;
    return  (-1.);
}

/* Prepare
 */

struct prepare_watcher {
    ev_prepare evw;
};

static void prepare_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct prepare_watcher *pw = watcher_get_data (w);
    ev_prepare_start (loop, &pw->evw);
}

static void prepare_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct prepare_watcher *pw = watcher_get_data (w);
    ev_prepare_stop (loop, &pw->evw);
}

static bool prepare_watcher_is_active (flux_watcher_t *w)
{
    struct prepare_watcher *pw = watcher_get_data (w);
    return ev_is_active (&pw->evw);
}

static void prepare_watcher_cb (struct ev_loop *loop,
                                ev_prepare *evw,
                                int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops prepare_watcher_ops = {
    .start = prepare_watcher_start,
    .stop = prepare_watcher_stop,
    .is_active = prepare_watcher_is_active,
    .destroy = NULL,
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
    ev_prepare_init (&pw->evw, prepare_watcher_cb);
    pw->evw.data = w;

    return w;
}

/* Check
 */

struct check_watcher {
    ev_check evw;
};

static void check_watcher_set_priority (flux_watcher_t *w, int priority)
{
    struct check_watcher *cw = watcher_get_data (w);
    ev_set_priority (&cw->evw, priority);
}

static void check_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct check_watcher *cw = watcher_get_data (w);
    ev_check_start (loop, &cw->evw);
}

static void check_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct check_watcher *cw = watcher_get_data (w);
    ev_check_stop (loop, &cw->evw);
}

static bool check_watcher_is_active (flux_watcher_t *w)
{
    struct check_watcher *cw = watcher_get_data (w);
    return ev_is_active (&cw->evw);
}

static void check_watcher_cb (struct ev_loop *loop, ev_check *evw, int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops check_watcher_ops = {
    .set_priority = check_watcher_set_priority,
    .start = check_watcher_start,
    .stop = check_watcher_stop,
    .is_active = check_watcher_is_active,
    .destroy = NULL,
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
    ev_check_init (&cw->evw, check_watcher_cb);
    cw->evw.data = w;

    return w;
}

/* Idle
 */

struct idle_watcher {
    ev_idle evw;
};

static void idle_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct idle_watcher *iw = watcher_get_data (w);
    ev_idle_start (loop, &iw->evw);
}

static void idle_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct idle_watcher *iw = watcher_get_data (w);
    ev_idle_stop (loop, &iw->evw);
}

static bool idle_watcher_is_active (flux_watcher_t *w)
{
    struct idle_watcher *iw = watcher_get_data (w);
    return ev_is_active (&iw->evw);
}

static void idle_watcher_cb (struct ev_loop *loop, ev_idle *evw, int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops idle_watcher_ops = {
    .start = idle_watcher_start,
    .stop = idle_watcher_stop,
    .is_active = idle_watcher_is_active,
    .destroy = NULL,
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
    ev_idle_init (&iw->evw, idle_watcher_cb);
    iw->evw.data = w;

    return w;
}

/* Child
 */

struct child_watcher {
    ev_child evw;
};

static void child_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct child_watcher *cw = watcher_get_data (w);
    ev_child_start (loop, &cw->evw);
}

static void child_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct child_watcher *cw = watcher_get_data (w);
    ev_child_stop (loop, &cw->evw);
}

static bool child_watcher_is_active (flux_watcher_t *w)
{
    struct child_watcher *cw = watcher_get_data (w);
    return ev_is_active (&cw->evw);
}

static void child_watcher_cb (struct ev_loop *loop, ev_child *evw, int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops child_watcher_ops = {
    .start = child_watcher_start,
    .stop = child_watcher_stop,
    .is_active = child_watcher_is_active,
    .destroy = NULL,
};

flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                           int pid,
                                           bool trace,
                                           flux_watcher_f cb,
                                           void *arg)
{
    flux_watcher_t *w;
    struct child_watcher *cw;

    if (!ev_is_default_loop (reactor_get_loop (r))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(w = watcher_create (r, sizeof (*cw), &child_watcher_ops, cb, arg)))
        return NULL;
    cw = watcher_get_data (w);
    ev_child_init (&cw->evw, child_watcher_cb, pid, trace ? 1 : 0);
    cw->evw.data = w;

    return w;
}

int flux_child_watcher_get_rpid (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &child_watcher_ops) {
        errno = EINVAL;
        return -1;
    }
    struct child_watcher *cw = watcher_get_data (w);
    return cw->evw.rpid;
}

int flux_child_watcher_get_rstatus (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &child_watcher_ops) {
        errno = EINVAL;
        return -1;
    }
    struct child_watcher *cw = watcher_get_data (w);
    return cw->evw.rstatus;
}

/* Signal
 */

struct signal_watcher {
    ev_signal evw;
};

static void signal_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct signal_watcher *sw = watcher_get_data (w);
    ev_signal_start (loop, &sw->evw);
}

static void signal_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct signal_watcher *sw = watcher_get_data (w);
    ev_signal_stop (loop, &sw->evw);
}

static bool signal_watcher_is_active (flux_watcher_t *w)
{
    struct signal_watcher *sw = watcher_get_data (w);
    return ev_is_active (&sw->evw);
}

static void signal_watcher_cb (struct ev_loop *loop,
                               ev_signal *evw,
                               int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops signal_watcher_ops = {
    .start = signal_watcher_start,
    .stop = signal_watcher_stop,
    .is_active = signal_watcher_is_active,
    .destroy = NULL,
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
    ev_signal_init (&sw->evw, signal_watcher_cb, signum);
    sw->evw.data = w;

    return w;
}

int flux_signal_watcher_get_signum (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &signal_watcher_ops) {
        errno = EINVAL;
        return (-1);
    }
    struct signal_watcher *sw = watcher_get_data (w);
    return sw->evw.signum;
}

/* Stat
 */

struct stat_watcher {
    ev_stat evw;
};

static void stat_watcher_start (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct stat_watcher *sw = watcher_get_data (w);
    ev_stat_start (loop, &sw->evw);
}

static void stat_watcher_stop (flux_watcher_t *w)
{
    struct ev_loop *loop = watcher_get_ev (w);
    struct stat_watcher *sw = watcher_get_data (w);
    ev_stat_stop (loop, &sw->evw);
}

static bool stat_watcher_is_active (flux_watcher_t *w)
{
    struct stat_watcher *sw = watcher_get_data (w);
    return ev_is_active (&sw->evw);
}

static void stat_watcher_cb (struct ev_loop *loop, ev_stat *evw, int revents)
{
    struct flux_watcher *w = evw->data;
    watcher_call_ev (w, revents);
}

static struct flux_watcher_ops stat_watcher_ops = {
    .start = stat_watcher_start,
    .stop = stat_watcher_stop,
    .is_active = stat_watcher_is_active,
    .destroy = NULL,
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
    ev_stat_init (&sw->evw, stat_watcher_cb, path, interval);
    sw->evw.data = w;

    return w;
}

void flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                  struct stat *stat,
                                  struct stat *prev)
{
    struct stat_watcher *sw = watcher_get_data (w);
    if (watcher_get_ops (w) == &stat_watcher_ops) {
        if (stat)
            *stat = sw->evw.attr;
        if (prev)
            *prev = sw->evw.prev;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
