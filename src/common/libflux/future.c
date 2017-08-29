/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <czmq.h>

#include "future.h"

struct now_context {
    flux_t *h;              // (optional) cloned flux_t handle
    flux_reactor_t *r;      // reactor created for this get/check
    flux_watcher_t *timer;  // timer watcher (if timeout set)
    bool init_called;       // other watchers configured (if init set)
    bool running;
};

struct then_context {
    flux_reactor_t *r;      // external reactor for then
    flux_watcher_t *timer;  // timer watcher (if timeout set)
    flux_watcher_t *prepare;// doorbell for fulfill
    flux_watcher_t *check;
    flux_watcher_t *idle;
    flux_continuation_f continuation;
    void *continuation_arg;
};

struct flux_future {
    flux_reactor_t *r;
    flux_t *h;
    zhash_t *aux;
    int aux_anon_ctr;
    void *result;
    flux_free_f result_free;
    int result_errnum;
    flux_future_init_f init;
    void *init_arg;
    struct now_context *now;
    struct then_context *then;
    bool result_valid;
    bool result_errnum_valid;
};

static void prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg);
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg);
static void now_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg);
static void then_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg);

/* "now" reactor context - used for flux_future_wait_on()
 * This is set up lazily; wait until the user calls flux_future_wait_on().
 * N.B. _wait_on() can be called multiple times (with different timeouts).
 */

static void now_context_destroy (struct now_context *now)
{
    if (now) {
        flux_watcher_destroy (now->timer);
        flux_reactor_destroy (now->r);
        flux_close (now->h);
        free (now);
    }
}

static struct now_context *now_context_create (void)
{
    struct now_context *now = calloc (1, sizeof (*now));
    if (!now) {
        errno = ENOMEM;
        goto error;
    }
    if (!(now->r = flux_reactor_create (0)))
        goto error;
    return now;
error:
    now_context_destroy (now);
    return NULL;
}

static int now_context_set_timeout (struct now_context *now,
                                    double timeout, void *arg)
{
    if (now) {
        if (timeout < 0.)       // disable
            flux_watcher_stop (now->timer);
        else {
            if (!now->timer) {  // set
                now->timer = flux_timer_watcher_create (now->r, timeout, 0.,
                                                        now_timer_cb, arg);
                if (!now->timer)
                    return -1;
            }
            else {              // reset
                flux_timer_watcher_reset (now->timer, timeout, 0.);
            }
            flux_watcher_start (now->timer);
        }
    }
    return 0;
}

static void now_context_clear_timer (struct now_context *now)
{
    if (now)
        flux_watcher_stop (now->timer);
}

/* "then" reactor context - used for continuation
 * This is set up lazily; wait until the user calls flux_future_then().
 * N.B. then() can only be called once.
 */

static void then_context_destroy (struct then_context *then)
{
    if (then) {
        flux_watcher_destroy (then->timer);
        flux_watcher_destroy (then->prepare);
        flux_watcher_destroy (then->check);
        flux_watcher_destroy (then->idle);
        free (then);
    }
}

static struct then_context *then_context_create (flux_reactor_t *r, void *arg)
{
    struct then_context *then = calloc (1, sizeof (*then));
    if (!then) {
        errno = ENOMEM;
        goto error;
    }
    then->r = r;
    if (!(then->prepare = flux_prepare_watcher_create (r, prepare_cb, arg)))
        goto error;
    if (!(then->check = flux_check_watcher_create (r, check_cb, arg)))
        goto error;
    if (!(then->idle = flux_idle_watcher_create (r, NULL, NULL)))
        goto error;
    flux_watcher_start (then->prepare);
    flux_watcher_start (then->check);
    return then;
error:
    then_context_destroy (then);
    return NULL;
}

static int then_context_set_timeout (struct then_context *then,
                                     double timeout, void *arg)
{
    assert (then != NULL);
    assert (then->timer == NULL);
    assert (timeout >= 0.);
    if (!(then->timer = flux_timer_watcher_create (then->r, timeout, 0.,
                                                   then_timer_cb, arg)))
        return -1;
    flux_watcher_start (then->timer);
    return 0;
}

static void then_context_clear_timer (struct then_context *then)
{
    if (then)
        flux_watcher_stop (then->timer);
}

/* Destroy a future.
 */
void flux_future_destroy (flux_future_t *f)
{
    int saved_errno = errno;
    if (f) {
        zhash_destroy (&f->aux);
        if (f->result) {
            if (f->result_free)
                f->result_free (f->result);
        }
        now_context_destroy (f->now);
        then_context_destroy (f->then);
        free (f);
    }
    errno = saved_errno;
}

/* Create a future.
 */
flux_future_t *flux_future_create (flux_reactor_t *r,
                                   flux_future_init_f cb, void *arg)
{
    flux_future_t *f = calloc (1, sizeof (*f));
    if (!f) {
        errno = ENOMEM;
        goto error;
    }
    f->init = cb;
    f->init_arg = arg;
    f->r = r;
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

/* Set flux handle
 */
void flux_future_set_flux (flux_future_t *f, flux_t *h)
{
    if (f)
        f->h = h;
}

/* Context dependent get of flux handle.
 * If "now" context, a one-off message dispatcher needs to be created to
 * go with the one-off reactor, hence flux_clone()
 */
flux_t *flux_future_get_flux (flux_future_t *f)
{
    flux_t *h = NULL;

    if (!f || !f->h) {
        errno = EINVAL;
        goto done;
    }
    if (!f->now || !f->now->running) {
        h = f->h;
    }
    else {
        if (!f->now->h) {
            if (!(f->now->h = flux_clone (f->h)))
                goto done;
            if (flux_set_reactor (f->now->h, f->now->r) < 0)
                goto done;
        }
        h = f->now->h;
    }
done:
    return h;
}

/* Block until future is fulfilled or timeout expires.
 * This function can be called multiple times, with different timeouts.
 * If timeout <= 0., there is no timeout.
 * If timeout == 0., time out immediately if future has not been fulfilled.
 * If timeout > 0., lazily set up the "now" reactor context (first call)
 * and run that reactor until fulfilled or error.  If timer expires,
 * don't fulfill the future; user might want to defer to continuation.
 * If flux_t handle is used, any messages not "consumed" by the future
 * have to go back to the parent handle with flux_dispatch_requeue().
 */
int flux_future_wait_for (flux_future_t *f, double timeout)
{
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!f->result_valid && !f->result_errnum_valid) {
        if (timeout == 0.) { // don't setup 'now' context in this case
            errno = ETIMEDOUT;
            return -1;
        }
        if (!f->now) {
            if (!(f->now = now_context_create ()))
                return -1;
        }
        if (timeout >= 0.) {
            if (now_context_set_timeout (f->now, timeout, f) < 0)
                return -1;
        }
        f->now->running = true;
        if (f->init && !f->now->init_called) {
            f->init (f, f->now->r, f->init_arg); // might set error
            f->now->init_called = true;
        }
        if (!f->result_valid && !f->result_errnum_valid) {
            if (flux_reactor_run (f->now->r, 0) < 0)
                return -1; // errno set by now_timer_cb or other watcher
        }
        if (f->now->h)
            flux_dispatch_requeue (f->now->h);
        f->now->running = false;
    }
    if (!f->result_valid && !f->result_errnum_valid)
        return -1;
    return 0;
}

/* Block until future is fulfilled if not already.
 * Then return either result or error depending on how it was fulfilled.
 */
int flux_future_get (flux_future_t *f, void *result)
{
    if (flux_future_wait_for (f, -1.0) < 0) // no timeout
        return -1;
    if (f->result_errnum_valid) {
        errno = f->result_errnum;
        return -1;
    }
    if (result)
        *(void **)result = f->result;
    return 0;
}

/* Set up continuation to run once future is fulfilled.
 * Lazily set up the "then" reactor context.
 * If timer expires, fulfill the future with ETIMEDOUT error.
 * This function can only be called once.
 */
int flux_future_then (flux_future_t *f, double timeout,
                      flux_continuation_f cb, void *arg)
{
    if (!f || !f->r || !cb || f->then != NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!(f->then = then_context_create (f->r, f)))
        return -1;
    if (timeout >= 0. && then_context_set_timeout (f->then, timeout, f) < 0)
        return -1;
    f->then->continuation = cb;
    f->then->continuation_arg = arg;
    if (f->init)
        f->init (f, f->then->r, f->init_arg); // might set error
    if (f->result_errnum_valid) {
        errno = f->result_errnum;
        return -1;
    }
    return 0;
}

/* Retrieve 'aux' object by name.  Name cannot be NULL.
 */
void *flux_future_aux_get (flux_future_t *f, const char *name)
{
    void *rv;

    if (!f) {
        errno = EINVAL;
        return NULL;
    }
    if (!f->aux) {
        errno = ENOENT;
        return NULL;
    }
    /* zhash_lookup won't set errno if not found */
    if (!(rv = zhash_lookup (f->aux, name)))
        errno = ENOENT;
    return rv;
}

/* Store 'aux' object by name.  Allow "anonymous" (name=NULL) objects to
 * be set - useful for adding destructors for watchers created in init
 * function, which may be called in both "now" and "then" contexts without
 * knowing which it is.
 */
int flux_future_aux_set (flux_future_t *f, const char *name,
                         void *aux, flux_free_f destroy)
{
    char name_buf[32];

    if (!f || (!name && !destroy)) {
        errno = EINVAL;
        return -1;
    }
    if (!f->aux)
        f->aux = zhash_new ();
    if (!f->aux) {
        errno = ENOMEM;
        return -1;
    }
    if (!name) {
        snprintf (name_buf, sizeof (name_buf), "anon.%d", f->aux_anon_ctr++);
        name = name_buf;
    }
    zhash_delete (f->aux, name);
    if (zhash_insert (f->aux, name, aux) < 0) {
        errno = ENOMEM;
        return -1;
    }
    zhash_freefn (f->aux, name, destroy);
    return 0;
}

void flux_future_fulfill (flux_future_t *f, void *result, flux_free_f free_fn)
{
    if (f) {
        if (f->result) {
            if (f->result_free)
                f->result_free (f->result);
        }
        f->result = result;
        f->result_free = free_fn;
        f->result_valid = true;
        now_context_clear_timer (f->now);
        then_context_clear_timer (f->then);
        if (f->now)
            flux_reactor_stop (f->now->r);
        /* in "then" context, the main reactor prepare/check/idle watchers
         * will run continuation in the next reactor loop for fairness.
         */
    }
}

void flux_future_fulfill_error (flux_future_t *f, int errnum)
{
    if (f) {
        f->result_errnum = errnum;
        f->result_errnum_valid = true;
        now_context_clear_timer (f->now);
        then_context_clear_timer (f->then);
        if (f->now)
            flux_reactor_stop (f->now->r);
        /* in "then" context, the main reactor prepare/check/idle watchers
         * will run continuation in the next reactor loop for fairness.
         */
    }
}

/* timer - for flux_future_then() timeout
 * fulfill the future with an error
 */
static void then_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    flux_future_t *f = arg;
    flux_future_fulfill_error (f, ETIMEDOUT);
}

/* timer - for flux_future_wait_for() timeout
 * stop the reactor but don't fulfill the future
 */
static void now_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    errno = ETIMEDOUT;
    flux_reactor_stop_error (r);
}

/* prepare - if results are ready, ensure loop doesn't block
 * so check can call continuation on next loop iteration
 */
static void prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    flux_future_t *f = arg;

    assert (f->then != NULL);

    if (f->result_valid || f->result_errnum_valid)
        flux_watcher_start (f->then->idle); // prevent reactor from blocking
}

/* check - if results are ready, call the continuation
 */
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    flux_future_t *f = arg;

    assert (f->then != NULL);

    flux_watcher_stop (f->then->idle);
    if (f->result_valid || f->result_errnum_valid) {
        flux_watcher_stop (f->then->timer);
        flux_watcher_stop (f->then->prepare);
        flux_watcher_stop (f->then->check);
        if (f->then->continuation)
            f->then->continuation (f, f->then->continuation_arg);
        // N.B. callback might destroy future
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
