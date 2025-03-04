/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
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
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <flux/core.h>

#ifndef EDEADLOCK
#define EDEADLOCK EDEADLK
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/aux.h"

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
    double timeout;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    bool init_called;
    flux_continuation_f continuation;
    void *continuation_arg;
};

struct future_result {
    bool is_error;             /* if should return value or errnum */
    int errnum;
    char *errnum_string;
    void *value;
    flux_free_f value_free;
};

struct flux_future {
    flux_reactor_t *r;
    flux_t *h;
    struct aux_item *aux;
    struct future_result result;
    bool result_valid;
    int fatal_errnum;
    char *fatal_errnum_string;
    bool fatal_errnum_valid;
    flux_future_init_f init;
    void *init_arg;
    struct now_context *now;
    struct then_context *then;
    zlist_t *queue;
    flux_future_t *embed;
    int refcount;
};

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg);
static void now_timer_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg);
static void then_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg);

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
    struct now_context *now;

    if (!(now = calloc (1, sizeof (*now))))
        return NULL;
    if (!(now->r = flux_reactor_create (0)))
        goto error;
    return now;
error:
    now_context_destroy (now);
    return NULL;
}

static int now_context_set_timeout (struct now_context *now,
                                    double timeout,
                                    void *arg)
{
    if (now) {
        if (timeout < 0.)       // disable
            flux_watcher_stop (now->timer);
        else {
            if (!now->timer) {  // set
                now->timer = flux_timer_watcher_create (now->r,
                                                        timeout,
                                                        0.,
                                                        now_timer_cb,
                                                        arg);
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
        flux_watcher_destroy (then->check);
        flux_watcher_destroy (then->idle);
        free (then);
    }
}

static struct then_context *then_context_create (flux_reactor_t *r, void *arg)
{
    struct then_context *then;

    if (!(then = calloc (1, sizeof (*then))))
        return NULL;
    then->r = r;
    if (!(then->check = flux_check_watcher_create (r, check_cb, arg)))
        goto error;
    if (!(then->idle = flux_idle_watcher_create (r, NULL, NULL)))
        goto error;
    return then;
error:
    then_context_destroy (then);
    return NULL;
}

static void then_context_start (struct then_context *then)
{
    flux_watcher_start (then->idle); // prevent reactor from blocking
    flux_watcher_start (then->check);
}

static void then_context_stop (struct then_context *then)
{
    flux_watcher_stop (then->idle);
    flux_watcher_stop (then->check);
}

static int then_context_set_timeout (struct then_context *then,
                                     double timeout,
                                     void *arg)
{
    if (then) {
        then->timeout = timeout;
        if (timeout < 0.)
            flux_watcher_stop (then->timer);
        else {
            if (!then->timer) {
                then->timer = flux_timer_watcher_create (then->r,
                                                         0.,
                                                         timeout,
                                                         then_timer_cb,
                                                         arg);
                if (!then->timer)
                    return -1;
            }
            flux_timer_watcher_again (then->timer);
        }
    }
    return 0;
}

static void init_result (struct future_result *fs)
{
    fs->is_error = false;
    fs->errnum = 0;
    fs->errnum_string = NULL;
    fs->value = NULL;
    fs->value_free = NULL;
}

static void clear_result (struct future_result *fs)
{
    if (fs->value && fs->value_free)
        fs->value_free (fs->value);
    if (fs->errnum_string)
        free (fs->errnum_string);
    init_result (fs);
}

static void set_result_value (struct future_result *fs,
                              void *value,
                              flux_free_f value_free)
{
    assert (fs->is_error == false);
    fs->value = value;
    fs->value_free = value_free;
}

static int set_result_errnum (struct future_result *fs,
                              int errnum,
                              const char *errstr)
{
    assert (!fs->value && !fs->value_free);
    fs->errnum = errnum;
    if (errstr && !(fs->errnum_string = strdup (errstr))) {
        int save_errno = errno;
        clear_result (fs);
        errno = save_errno;
        return -1;
    }
    fs->is_error = true;
    return 0;
}

static void move_result (struct future_result *dst, struct future_result *src)
{
    dst->is_error = src->is_error;
    dst->errnum = src->errnum;
    dst->errnum_string = src->errnum_string;
    dst->value = src->value;
    dst->value_free = src->value_free;
    init_result (src);
}

static void future_result_destroy (void *data)
{
    struct future_result *fs = data;
    if (fs) {
        clear_result (fs);
        free (fs);
    }
}

static struct future_result *future_result_value_create (void *value,
                                                         flux_free_f value_free)
{
    struct future_result *fs = calloc (1, sizeof (*fs));
    if (!fs)
        return NULL;
    set_result_value (fs, value, value_free);
    return fs;
}

static struct future_result *future_result_errnum_create (int errnum,
                                                          const char *errstr)
{
    struct future_result *fs = calloc (1, sizeof (*fs));
    if (!fs)
        return NULL;
    if (set_result_errnum (fs, errnum, errstr) < 0) {
        int save_errno = errno;
        clear_result (fs);
        free (fs);
        errno = save_errno;
        return NULL;
    }
    return fs;
}

/* Destroy a future.
 */
void flux_future_destroy (flux_future_t *f)
{
    if (f && (--f->refcount == 0)) {
        int saved_errno = errno;
        flux_future_destroy (f->embed);
        aux_destroy (&f->aux);
        clear_result (&f->result);
        free (f->fatal_errnum_string);
        now_context_destroy (f->now);
        then_context_destroy (f->then);
        zlist_destroy (&f->queue);
        flux_decref (f->h);
        free (f);
        errno = saved_errno;
    }
}

/* Create a future.
 */
flux_future_t *flux_future_create (flux_future_init_f cb, void *arg)
{
    flux_future_t *f;

    if (!(f = calloc (1, sizeof (*f))))
        return NULL;
    f->init = cb;
    f->init_arg = arg;
    f->queue = NULL;
    f->embed = NULL;
    f->refcount = 1;
    return f;
}

void flux_future_incref (flux_future_t *f)
{
    if (f)
        f->refcount++;
}

void flux_future_decref (flux_future_t *f)
{
    flux_future_destroy (f);
}

static void post_fulfill (flux_future_t *f)
{
    now_context_clear_timer (f->now);
    if (f->now)
        flux_reactor_stop (f->now->r);
    if (f->then)
        then_context_start (f->then);
}

/* Reset (unfulfill) a future.
 */
void flux_future_reset (flux_future_t *f)
{
    if (f) {
        clear_result (&f->result);
        f->result_valid = false;
        if (f->then) {
            then_context_stop (f->then);
            (void)then_context_set_timeout (f->then, f->then->timeout, f);
        }
        if (f->queue && zlist_size (f->queue) > 0) {
            struct future_result *fs = zlist_pop (f->queue);
            move_result (&f->result, fs);
            f->result_valid = true;
            future_result_destroy (fs);
            post_fulfill (f);
        }
    }
}

/* Set the flux reactor to be used for 'then' context.
 * In 'now' context, reactor will be a temporary one.
 */
void flux_future_set_reactor (flux_future_t *f, flux_reactor_t *r)
{
    if (f)
        f->r = r;
}

/* Context dependent get of reactor.
 * If "now" context, return one off reactor.
 * If "then" context, return the one that was set.
 */
flux_reactor_t *flux_future_get_reactor (flux_future_t *f)
{
    flux_reactor_t *r;

    if (!f)
        goto inval;
    if (!f->now || !f->now->running) {
        if (!f->r)
            goto inval;
        r = f->r;
    }
    else {
        if (!f->now->r)
            goto inval;
        r = f->now->r;
    }
    return r;
inval:
    errno = EINVAL;
    return NULL;
}

/* Set the flux handle to be used for 'then' context.
 * In 'now' context, handle will be a clone of this one,
 * associated with the temporary reactor.
 */
void flux_future_set_flux (flux_future_t *f, flux_t *h)
{
    if (f) {
        flux_decref (f->h);
        f->h = flux_incref (h);
        if (h && !f->r)
            f->r = flux_get_reactor (h);
    }
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

static bool future_is_ready (flux_future_t *f)
{
    return (f->result_valid || f->fatal_errnum_valid);
}

/* Block until future is fulfilled or timeout expires.
 * This function can be called multiple times, with different timeouts.
 * If timeout < 0., there is no timeout.
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
    if (!future_is_ready (f)) {
        if (timeout == 0.) { // don't setup 'now' context in this case
            errno = ETIMEDOUT;
            return -1;
        }
        if (!f->now) {
            if (!(f->now = now_context_create ()))
                return -1;
        }
        if (now_context_set_timeout (f->now, timeout, f) < 0)
            return -1;
        f->now->running = true;
        if (f->init && !f->now->init_called) {
            f->init (f, f->init_arg); // might set error
            f->now->init_called = true;
        }
        if (!future_is_ready (f)) {
            if (flux_reactor_run (f->now->r, 0) < 0)
                return -1; // errno set by now_timer_cb or other watcher
        }
        if (f->now->h)
            flux_dispatch_requeue (f->now->h);
        f->now->running = false;
    }
    if (!future_is_ready (f)) {
        /* This block of code is reached b/c flux_reactor_run() on the
         * "now" reactor returned >= 0 AND the future was
         * not fulfilled, e.g.
         * - no init callback was registered when future was created
         * - the init callback didn't register any watchers
         * - the registered watchers stopped themselves or called
         *   flux_reactor_stop() without fulfilling the future.
         * Return EDEADLOCK indicating that the future cannot be
         * fulfilled.
         */
        errno = EDEADLOCK;
        return -1;
    }
    return 0;
}


/* Return true if future is fulfilled and flux_future_get() will not block.
 */
bool flux_future_is_ready (flux_future_t *f)
{
    if (f && future_is_ready (f))
        return true;
    return false;
}

/* Block until future is fulfilled if not already.
 * Then return either result or error depending on how it was fulfilled.
 */
int flux_future_get (flux_future_t *f, const void **result)
{
    if (flux_future_wait_for (f, -1.0) < 0) // no timeout
        return -1;
    if (f->fatal_errnum_valid) {
        errno = f->fatal_errnum;
        return -1;
    }
    if (f->result_valid) {
        if (f->result.is_error) {
            errno = f->result.errnum;
            return -1;
        }
        else {
            if (result)
                (*result) = f->result.value;
        }
    }
    return 0;
}

/* Set up continuation to run once future is fulfilled.
 * Lazily set up the "then" reactor context.
 * If timer expires, fulfill the future with ETIMEDOUT error.
 */
int flux_future_then (flux_future_t *f,
                      double timeout,
                      flux_continuation_f cb,
                      void *arg)
{
    if (!f || !f->r || !cb) {
        errno = EINVAL;
        return -1;
    }
    if (!f->then) {
        if (!(f->then = then_context_create (f->r, f)))
            return -1;
    }
    if (future_is_ready (f))
        then_context_start (f->then);
    if (then_context_set_timeout (f->then, timeout, f) < 0)
        return -1;
    f->then->continuation = cb;
    f->then->continuation_arg = arg;
    if (f->init && !f->then->init_called) {
        f->init (f, f->init_arg); // might set error
        f->then->init_called = true;
    }
    return 0;
}

/* Retrieve 'aux' object by name.
 */
void *flux_future_aux_get (flux_future_t *f, const char *name)
{
    void *result = NULL;
    if (!f) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = aux_get (f->aux, name)) && f->embed)
        result = flux_future_aux_get (f->embed, name);
    return result;
}

/* Store 'aux' object by name.
 */
int flux_future_aux_set (flux_future_t *f,
                         const char *name,
                         void *aux,
                         flux_free_f destroy)
{
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&f->aux, name, aux, destroy);
}

static void fulfill_internal_error (flux_future_t *f,
                                    void *result,
                                    flux_free_f free_fn,
                                    int errnum)
{
    if (result && free_fn)
        free_fn (result);
    flux_future_fatal_error (f, errnum, NULL);
}

static int queue_result (flux_future_t *f, struct future_result *fs)
{
    if (!f->queue) {
        if (!(f->queue = zlist_new ())) {
            errno = ENOMEM;
            return -1;
        }
    }
    zlist_append (f->queue, fs);
    zlist_freefn (f->queue, fs, future_result_destroy, true);
    return 0;
}

void flux_future_fulfill (flux_future_t *f, void *result, flux_free_f free_fn)
{
    if (f) {
        if (f->fatal_errnum_valid)
            return;
        if (f->result_valid) {
            struct future_result *fs;
            if (!(fs = future_result_value_create (result, free_fn))) {
                fulfill_internal_error (f, result, free_fn, errno);
                return;
            }
            if (queue_result (f, fs) < 0) {
                fulfill_internal_error (f, result, free_fn, errno);
                future_result_destroy (fs);
                return;
            }
        }
        else {
            set_result_value (&f->result, result, free_fn);
            f->result_valid = true;
        }
        post_fulfill (f);
    }
}

void flux_future_fulfill_error (flux_future_t *f,
                                int errnum,
                                const char *errstr)
{
    if (f) {
        if (f->fatal_errnum_valid)
            return;
        if (f->result_valid) {
            struct future_result *fs;
            if (!(fs = future_result_errnum_create (errnum, errstr))) {
                fulfill_internal_error (f, NULL, NULL, errno);
                return;
            }
            if (queue_result (f, fs) < 0) {
                fulfill_internal_error (f, NULL, NULL, errno);
                future_result_destroy (fs);
                return;
            }
        }
        else {
            clear_result (&f->result);
            if (set_result_errnum (&f->result, errnum, errstr) < 0) {
                flux_future_fatal_error (f, errno, NULL);
                return;
            }
            f->result_valid = true;
        }
        post_fulfill (f);
    }
}

int flux_future_fulfill_with (flux_future_t *f, flux_future_t *p)
{
    if (!f || !p || (f == p)) {
        errno = EINVAL;
        return -1;
    }
    /*  Only allow the same embedded future to be used for multiple
     *   fulfillment. This constrains the usefulness of fulfill_with
     *   a small amount to avoid the need to keep a list of embedded
     *   futures that go with each result.
     */
    if (f->embed && (p != f->embed)) {
        errno = EEXIST;
        return -1;
    }
    if (!future_is_ready (p)) {
        errno = EAGAIN;
        return -1;
    }
    /*  Copy fatal error result, normal result, or error result into `f`
     *   (in that order of precedence). The "result" is copied using futures
     *   API to ensure fulfillment (including multiple fulfillment) is posted
     *   to `f` properly.
     */
    if (p->fatal_errnum_valid)
        flux_future_fatal_error (f, p->fatal_errnum, p->fatal_errnum_string);
    else if (p->result.is_error)
        flux_future_fulfill_error (f,
                                   p->result.errnum,
                                   p->result.errnum_string);
    else {
        /*  Normal result, if result has a free_fn registered, then we have
         *   to steal the reference for the result. We do this by copying
         *   the free_fn to 'f' and nullifying it in 'p'.
         */
        flux_future_fulfill (f, p->result.value, p->result.value_free);
        p->result.value_free = NULL;
    }
    if (!f->embed) {
        f->embed = p;
        flux_future_incref (p);
    }
    return 0;
}

void flux_future_fatal_error (flux_future_t *f, int errnum, const char *errstr)
{
    if (f) {
        if (!f->fatal_errnum_valid) {
            f->fatal_errnum = errnum;
            /* if ENOMEM here, nothing we can do b/c we're in
             * flux_future_fatal_error already */
            if (errstr)
                f->fatal_errnum_string = strdup (errstr);
            f->fatal_errnum_valid = true;
        }
        post_fulfill (f);
    }
}

bool flux_future_has_error (flux_future_t *f)
{
    if (f) {
        /* fatal errnum take precedence over any future
         * fulfillments */
        if (f->fatal_errnum_valid)
            return true;
        else if (f->result_valid) {
            /* future contains a valid fulfillment, must check if it
             * is a error fulfillment.
             */
            if (f->result.is_error)
                return true;
        }
    }
    return false;
}

const char *flux_future_error_string (flux_future_t *f)
{
    if (f) {
        /* fatal errnum take precedence over any future
         * fulfillments */
        if (f->fatal_errnum_valid) {
            if (f->fatal_errnum_string)
                return f->fatal_errnum_string;
            return flux_strerror (f->fatal_errnum);
        }
        else if (f->result_valid && f->result.is_error) {
            /* future contains a valid error fulfillment.  Return the
             * optional error string or flux_strerror() of the errnum.
             */
            if (f->result.errnum_string)
                return f->result.errnum_string;
            return flux_strerror (f->result.errnum);
        }
    }
    return NULL;
}

/* timer - for flux_future_then() timeout
 * fulfill the future with an error
 */
static void then_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    flux_future_t *f = arg;
    flux_future_fulfill_error (f, ETIMEDOUT, NULL);
}

/* timer - for flux_future_wait_for() timeout
 * stop the reactor but don't fulfill the future
 */
static void now_timer_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    errno = ETIMEDOUT;
    flux_reactor_stop_error (r);
}

/* check - if results are ready, call the continuation
 */
static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    flux_future_t *f = arg;

    assert (f->then != NULL);

    flux_watcher_stop (f->then->timer);
    then_context_stop (f->then);
    if (f->then->continuation)
        f->then->continuation (f, f->then->continuation_arg);
    // N.B. callback might destroy future
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
