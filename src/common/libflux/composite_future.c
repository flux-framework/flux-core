/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

/*  Type-specific data for a composite future:
 */
struct composite_future {
    int seq;             /* sequence for anonymous children          */
    unsigned int any:1;  /* true if this future is a "wait any" type */
    zhash_t *children;   /* hash of child futures by name            */
};

static void composite_future_destroy (struct composite_future *f)
{
    if (f) {
        if (f->children)
            zhash_destroy (&f->children);
        free (f);
    }
}

static struct composite_future * composite_future_create (void)
{
    struct composite_future *cf = calloc (1, sizeof (*cf));
    if (cf == NULL)
        return NULL;
    if (!(cf->children = zhash_new ())) {
        free (cf);
        return (NULL);
    }
    return (cf);
}

/*  Return the embedded composite_future data from future `f`
 */
static struct composite_future * composite_get (flux_future_t *f)
{
    return flux_future_aux_get (f, "flux::composite");
}

/*
 *  Return true if all futures in this composite are ready
 */
static bool wait_all_is_ready (struct composite_future *cf, int *errnum)
{
    int err = 0;
    flux_future_t *f = zhash_first (cf->children);
    while (f) {
        if (!flux_future_is_ready (f))
            return (false);
        if (flux_future_get (f, NULL) < 0)
            err = errno;
        f = zhash_next (cf->children);
    }
    *errnum = err;
    return (true);
}

/*
 *  Return true if cf->any or if all futures are fulfilled.
 *  Sets *errp to returned errno if any future failed for wait_all
 *   case, or if 'f', the current future, has error set for
 *   wait_any case.
 */
static bool composite_is_ready (struct composite_future *cf,
                                flux_future_t *f,
                                int *errp)
{
    if (cf->any) {
        *errp = flux_future_get (f, NULL) < 0 ? errno : 0;
        return true;
    }
    return wait_all_is_ready (cf, errp);
}

/*  Continuation for children of a composition future -- simply check
 *   to see if the parent composite is "ready" and fulfill it if so.
 */
static void child_cb (flux_future_t *f, void *arg)
{
    int errnum;
    flux_future_t *parent = arg;
    struct composite_future *cf = composite_get (parent);
    if (!arg || !cf)
        return;
    if (composite_is_ready (cf, f, &errnum)) {
        if (errnum)
            flux_future_fulfill_error (parent, errnum, NULL);
        else
            flux_future_fulfill (parent, NULL, NULL);
    }
}

/*  Propagate the current reactor *and* flux_t handle context from
 *   future `f` to another future `next`.
 */
static void future_propagate_context (flux_future_t *f, flux_future_t *next)
{
    /*  Note: flux_future_set_flux(3) will *also* reset the reactor
     *   for the future `next` using `flux_get_reactor (handle)`.
     *   However, we still have to explicitly call set_reactor() here
     *   for the case where a flux_t handle is *not* currently set
     *   in the context of future `f` (e.g. if operating within a
     *   reactor only with no connection to flux)
     */
    flux_future_set_reactor (next, flux_future_get_reactor (f));
    flux_future_set_flux (next, flux_future_get_flux (f));
}

/*  Initialization callback for a composite future. Register then
 *   continuations for all child futures on active reactor.
 */
void composite_future_init (flux_future_t *f, void *arg)
{
    flux_future_t *child;
    struct composite_future *cf = arg;
    bool empty = true;
    if (cf == NULL) {
        errno = EINVAL;
        goto error;
    }
    /*
     *  Propagate current context of this composite future to all children
     *   so that the composite future's 'then' *or* 'now' context becomes
     *   a 'then' context for all children.
     */
    child = zhash_first (cf->children);
    while (child) {
        if (empty)
            empty = false;
        future_propagate_context (f, child);
        if (flux_future_then (child, -1., child_cb, (void *) f) < 0)
            goto error;
        child = zhash_next (cf->children);
    }
    /*  An empty wait_all future is fulfilled immediately since
     *   logically "all" child futures are fulfilled
     */
    if (empty && !cf->any)
        flux_future_fulfill (f, NULL, NULL);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

/*
 *  Construct a composite future.
 *  If the wait_any flag is 1 then make this a "wait any" composite.
 */
static flux_future_t *future_create_composite (int wait_any)
{
    struct composite_future *cf = composite_future_create ();
    flux_future_t *f = flux_future_create (composite_future_init, (void *) cf);
    if (!f
        || !cf
        || flux_future_aux_set (f,
                                "flux::composite",
                                cf,
                                (flux_free_f) composite_future_destroy) < 0) {
        composite_future_destroy (cf);
        flux_future_destroy (f);
        return (NULL);
    }
    cf->any = wait_any;
    return (f);
}

/*  Constructor for "wait_all" composite future.
 */
flux_future_t *flux_future_wait_all_create (void)
{
    return future_create_composite (0);
}

/*  Constructor for "wait_any" composite future
 */
flux_future_t *flux_future_wait_any_create (void)
{
    return future_create_composite (1);
}

int flux_future_push (flux_future_t *f, const char *name, flux_future_t *child)
{
    struct composite_future *cf = NULL;
    char *anon = NULL;
    int rc = -1;

    if (!f || !child || !(cf = composite_get (f))) {
        errno = EINVAL;
        return -1;
    }
    if (name == NULL) {
        if (asprintf (&anon, "%d", cf->seq++) < 0)
            return -1;
        name = anon;
    }
    if (zhash_insert (cf->children, name, child) < 0) {
        errno = EEXIST;
        goto done;
    }
    zhash_freefn (cf->children, name, (flux_free_f) flux_future_destroy);
    if (flux_future_aux_set (child, "flux::parent", f, NULL) < 0) {
        zhash_delete (cf->children, name);
        goto done;
    }
    rc = 0;
done:
    free (anon);
    return rc;
}

flux_future_t *flux_future_get_child (flux_future_t *f, const char *name)
{
    struct composite_future *cf = NULL;
    if (!f || !name || !(cf = composite_get (f))) {
        errno = EINVAL;
        return (NULL);
    }
    return zhash_lookup (cf->children, name);
}

const char *flux_future_first_child (flux_future_t *f)
{
    struct composite_future *cf = NULL;
    if (!f || !(cf = composite_get (f))) {
        errno = EINVAL;
        return (NULL);
    }
    if (!zhash_first (cf->children))
        return (NULL);
    return (zhash_cursor (cf->children));
}

const char *flux_future_next_child (flux_future_t *f)
{
    struct composite_future *cf = NULL;
    if (!f || !(cf = composite_get (f))) {
        errno = EINVAL;
        return (NULL);
    }
    if (!zhash_next (cf->children))
        return (NULL);
    return (zhash_cursor (cf->children));
}

/*  Chained futures support: */

/*
 *  Chained futures implementation:
 *
 *  When a chained future is created using flux_future_and_then() or
 *   flux_future_or_then() on a target future f, a chained_future structure
 *   (see below) is created and embedded in the aux data for f. The call
 *   returns an empty "next" future in the chain (cf->next) to the user.
 *   If the user calls both and_then() and or_then(), the same cf->next
 *   future is returned, since only one of these calls will be used.
 *
 *  The underlying then() callback for `f` is subsequently set to use
 *   chained_continuation() below, which will call `and_then()` on successful
 *   fulfillment of f (aka `prev`) or or_then() on failure. These continuations
 *   are passed `f, arg` as if a normal continuation was used with
 *   flux_future_then(3). These callbacks may use one of
 *   flux_future_continue(3) or flux_future_continue_error(3) to schedule
 *   fulfillment of the internal `cf->next` future based on a new
 *   intermediate future created during the continuation (e.g. when a
 *   new RPC call is started in the continuation, the future returned by
 *   that call is considered the intermediate future which will eventually
 *   fulfill cf->next).
 *
 *  flux_future_continue(f, f2) works by setting a then() callback
 *   on f to call fulfill_next() on the cf->next embedded in f.
 *   This results in `flux_future_fulfill_with (cf->next, f2)` immediately
 *   when f2 is fulfilled.
 *
 *  If flux_future_continue(3),flux_future_future_continue_error(3) are
 *   not used in the callback, then the default behavior is to immediately
 *   fulfill the next future with the current future. To avoid fulfilling
 *   the next future, e.g. if conditions are not met during multiple
 *   fulfillment, the caller may use flux_future_continue (f, NULL);
 *
 *  All of this simply allows the "next" future returned by and_then()
 *   or or_then() to be a placeholder for a future which can't be
 *   created yet, because it requires the result of a previous, but not
 *   yet complete, operation in the chain.
 */

struct continuation_info {
    flux_continuation_f cb;
    void *arg;
};

struct chained_future {
    int refcount;
    bool continued;
    flux_future_t *next;
    flux_future_t *prev;
    struct continuation_info and_then;
    struct continuation_info or_then;
};

/*
 *  Fulfill "next" future in a chain with the fulfilled future `f`.
 */
static void fulfill_next (flux_future_t *f, flux_future_t *next)
{
    /* NB: flux_future_fulfill_with(3) takes a reference to `f` on success.
     *  Since this function serves as an internal callback for `f` as a
     *  result of flux_future_continue(3), we destroy the implicit
     *  reference taken by flux_future_continue(3) here, since the
     *  next callback the user will see will be for the future `next`.
     */
    if (flux_future_fulfill_with (next, f) < 0)
        flux_future_fatal_error (next, errno,
            "fulfill_next: flux_future_fulfill_with failed");
    flux_future_destroy (f);
}

/*  Callback for chained continuations. Obtains the result of the completed
 *   "previous" future, then calls the appropriate "and_then" or "or_then"
 *   callback, or fulfill the "next" future with an error automatically.
 */
static void chained_continuation (flux_future_t *prev, void *arg)
{
    bool ran_callback = false;
    struct chained_future *cf = arg;

    /*  Reset cf->continued to handle multiple fulfillment of prev:
     */
    cf->continued = false;

    /*  Take a reference to prev in case it is destroyed during the
     *   and_then or or_then callback -- we need to access cf contents
     *   after these callbacks complete to determine if the future
     *   was continued.
     */
    flux_future_incref (prev);

    if (flux_future_get (prev, NULL) < 0) {
        /*  Handle "or" callback if set and return immediately */
        if (cf->or_then.cb) {
            (*cf->or_then.cb) (prev, cf->or_then.arg);
            ran_callback = true;
        }
    }
    else if (cf->and_then.cb) {
        (*cf->and_then.cb) (prev, cf->and_then.arg);
        ran_callback = true;
    }

    /*  Check if prev future was reset. If so return and allow this
     *   continuation to run again.
     */
    if (!flux_future_is_ready (prev))
        return;

    /*  If future prev was not continued with flux_future_continue()
     *   or flux_future_continue_error(), then fallback to continue
     *   cf->next using prev directly.
     */
    if (!cf->continued && flux_future_fulfill_with (cf->next, prev) < 0) {
        flux_future_fatal_error (cf->next,
                                 errno,
                                 "chained_continuation: fulfill_with failed");
    }

    /*  Release our reference to prev here (this may destroy prev)
     */
    flux_future_decref (prev);

    /*  Destroy prev here only if we didn't call a user's callback,
     *   e.g. one of or_then() or and_then() was not registered when the
     *   future was fulfilled with error or success respectively.
     *
     *  XXX: Look at this again if ownership model of flux_future_t changes.
     */
    if (!ran_callback)
        flux_future_destroy (prev);
}

static void chained_future_decref (struct chained_future *cf)
{
    if (--cf->refcount == 0) {
        int saved_errno = errno;
        free (cf);
        errno = saved_errno;
    }
}

/*  Initialization for a chained future. Get current reactor for this
 *   context and install it in "previous" future, _then_ set the "then"
 *   callback for that future to `chained_continuation()` which will
 *   call take the appropriate action for the result.
 */
static void chained_future_init (flux_future_t *f, void *arg)
{
    struct chained_future *cf = arg;
    if (cf == NULL
        || cf->prev == NULL
        || cf->next == NULL
        || !(flux_future_get_reactor (f))) {
        errno = EINVAL;
        goto error;
    }
    /*  Grab the reactor and flux_t handle (if any) for the current
     *   context of future 'f', and propagate it to the previous future
     *   in the chain. This ensures that the chain of "then" registrations
     *   are placed on the correct reactor (our main reactor if in 'then'
     *   context, or the temporary reactor in 'now' context), and that the
     *   cloned handle is used on these callbacks if we are in 'now'
     *   context.
     */
    future_propagate_context (f, cf->prev);

    /*  Now call flux_future_then(3) with our chained-future continuation
     *   function on the previous future in this chain. This allows
     *   a flux_future_get(3) on 'f' to block, while its antecedent
     *   futures are fulfilled asynchronously.
     */
    if (flux_future_then (cf->prev, -1., chained_continuation, cf) < 0)
        goto error;
    return;
error:
    /* Initialization failed. Fulfill f with error to indicate the failure,
     *  and pass the error up the chain to cf->next, since that is likely the
     *  future which has callbacks registered on it.
     */
    flux_future_fulfill_error (f, errno, NULL);
    fulfill_next (f, cf->next);
}

static void cf_next_destroy (struct chained_future *cf)
{
    /*  cf->next is being destroyed. If cf->prev is still active, destroy
     *  it now since no longer makes sense to trigger the cf->prev callback.
     */
    flux_future_destroy (cf->prev);
    cf->prev = NULL;

    /*  Release reference on cf taken by cf->next
     */
    chained_future_decref (cf);
}

/*  Allocate a chained future structure */
static struct chained_future *chained_future_alloc (void)
{
    struct chained_future *cf = calloc (1, sizeof (*cf));
    if (cf == NULL)
        return NULL;
    if (!(cf->next = flux_future_create (chained_future_init, (void *)cf))) {
        free (cf);
        return (NULL);
    }

    /*  If cf->next is destroyed then later cf->prev is fulfilled, this
     *  can cause use-after-free of cf->next in chained_continuation() and/or
     *  flux_future_continue(3). Additionally, a reasonable expectation
     *  is that destruction of cf->next will stop any previous future in
     *  the chain. Therefore, arrange to have this object notified when
     *  cf->next is destroyed so that cf->prev can also be destroyed
     *  (if it has not yet been destroyed already. That case is handled
     *   by nullifying cf->prev when prev is destroyed)
     */
    if (flux_future_aux_set (cf->next,
                             NULL,
                             cf,
                             (flux_free_f) cf_next_destroy) < 0) {
        flux_future_destroy (cf->next);
        free (cf);
        return NULL;
    }

    /*  Take one reference on cf for cf->next (released in cf_next_destroy())
     */
    cf->refcount = 1;
    return (cf);
}

static void nullify_prev (struct chained_future *cf)
{
    /*  cf->prev has been destroyed. Ensure we don't reference it again
     *  by nullifying the reference in cf.
     */
    cf->prev = NULL;

    /* Remove reference added by this callback */
    chained_future_decref (cf);
}

/*  Create a chained future on `f` by embedding a chained future
 *   structure as "flux::chained" in the aux hash.
 *
 *  The future `f` doesn't "own" the memory for cf->next,
 *   since this next future in the chain may be passed to the user
 *   or another continuation etc.
 */
static struct chained_future *chained_future_create (flux_future_t *f)
{
    struct chained_future *cf;

    /*  If future `f` is already chained, then just return the existing
     *  stored chained_future object:
     */
    if ((cf = flux_future_aux_get (f, "flux::chained")))
        return cf;

    /*  Otherwise, create one and store it:
     */
    if (!(cf = chained_future_alloc ())
        || flux_future_aux_set (f,
                                "flux::chained",
                               (void *) cf,
                               (flux_free_f) chained_future_decref) < 0) {
        chained_future_decref (cf);
        return NULL;
    }

    /* Increment refcount for cf->prev
     */
    cf->refcount++;
    cf->prev = f;

    /* Nullify cf->prev on destruction of f to notify cf_next_destroy() that
     *  prev was already destroyed. (See note in chained_future_alloc()).
     */
    if (flux_future_aux_set (f,
                             NULL,
                             cf,
                             (flux_free_f) nullify_prev) < 0) {
        chained_future_decref (cf);
        return NULL;
    }

    /* Increment refcount again for nullify_prev aux item. This is necessary
     *  since the order of aux_item destructors can't be guaranteed.
     *  This increment is done separately to avoid leaking cf in case of
     *  flux_future_aux_set() failure above.
     */
    cf->refcount++;

    /*
     * Ensure the empty "next" future we have just created inherits
     *  the same reactor (if any) and handle (if any) from the previous
     *  future in the chain `f`. If this is not done, then there may be
     *  no default reactor on which `flux_future_then(3)` can operate,
     *  and no default handle to clone in `flux_future_wait_for(3)`.
     */
    future_propagate_context (f, cf->next);
    return (cf);
}

static struct chained_future *chained_future_get (flux_future_t *f)
{
    return (flux_future_aux_get (f, "flux::chained"));
}

/* "Continue" the chained "next" future embedded in `prev` with the
 *  future `f` by setting the continuation of `f` to fulfill "next".
 *
 * Steals ownership of `f` so that its destruction can be tied to
 *  next. (`prev`, however, is free to be destroyed after this call)
 */
int flux_future_continue (flux_future_t *prev, flux_future_t *f)
{
    struct chained_future *cf = chained_future_get (prev);
    if (cf == NULL || !cf->next) {
        errno = EINVAL;
        return -1;
    }
    cf->continued = true;

    /*  If f is NULL then continue without fulfilling cf->next */
    if (f == NULL)
        return 0;

    /*  If f == prev, then we're continuing the next future with the
     *   currently fulfilled future. Just call fulfill_with() immediately
     *   and return, no need to propagate context or install a
     *   continuation.
     */
    if (f == prev)
        return flux_future_fulfill_with (cf->next, f);

    /*  Ensure that the reactor/handle for f matches the current reactor
     *   context for the previous future `prev`.
     */
    future_propagate_context (prev, f);

    /*  Set the "next" future in the chain (prev->next) to be fulfilled
     *   by the provided future `f` once it is fulfilled.
     */
    return flux_future_then (f, -1.,
                             (flux_continuation_f) fulfill_next,
                             cf->next);
}

/* "Continue" the chained "next" future embedded in `prev` with an error
 */
void flux_future_continue_error (flux_future_t *prev,
                                 int errnum,
                                 const char *errstr)
{
    struct chained_future *cf = chained_future_get (prev);
    if (cf && cf->next) {
        cf->continued = true;
        flux_future_fulfill_error (cf->next, errnum, errstr);
    }
}

int flux_future_fulfill_next (flux_future_t *f,
                              void *result,
                              flux_free_f free_fn)
{
    struct chained_future *cf = chained_future_get (f);
    if (cf == NULL || !cf->next) {
        errno = EINVAL;
        return -1;
    }
    cf->continued = true;
    flux_future_fulfill (cf->next, result, free_fn);
    return 0;
}

flux_future_t *flux_future_and_then (flux_future_t *prev,
                                     flux_continuation_f next_cb,
                                     void *arg)
{
    struct chained_future *cf = chained_future_create (prev);
    if (!cf)
        return NULL;
    cf->and_then.cb = next_cb;
    cf->and_then.arg = arg;
    return (cf->next);
}

flux_future_t *flux_future_or_then (flux_future_t *prev,
                                    flux_continuation_f or_cb,
                                    void *arg)
{
    struct chained_future *cf = chained_future_create (prev);
    if (!cf)
        return NULL;
    cf->or_then.cb = or_cb;
    cf->or_then.arg = arg;
    return (cf->next);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
