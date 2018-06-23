/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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
#include <czmq.h>

#include "future.h"

/*  Type-specific data for a composite future:
 */
struct composite_future {
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
static bool wait_all_is_ready (struct composite_future *cf)
{
    flux_future_t *f = zhash_first (cf->children);
    while (f) {
        if (!flux_future_is_ready (f))
            return (false);
        f = zhash_next (cf->children);
    }
    return (true);
}

/*  Continuation for children of a composition future -- simply check
 *   to see if the parent composite is "ready" and fulfill it if so.
 */
static void child_cb (flux_future_t *f, void *arg)
{
    flux_future_t *parent = arg;
    struct composite_future *cf = composite_get (parent);
    if (!arg || !cf)
        return;
    /*
     *  Fulfill the composite future if "wait any" is set, or all child
     *   futures are fulfilled:
     */
    if (cf->any || wait_all_is_ready (cf))
        flux_future_fulfill (parent, NULL, NULL);
}

/*  Initialization callback for a composite future. Register then
 *   continuations for all child futures on active reactor.
 */
void composite_future_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r;
    flux_future_t *child;
    struct composite_future *cf = arg;
    if (cf == NULL) {
        errno = EINVAL;
        goto error;
    }
    /*
     *  Get the current reactor for the context of this composite future,
     *   and install it on all child futures so that the composite future's
     *   'then' *or* 'now' context becomes a 'then' context for all children.
     */
    r = flux_future_get_reactor (f);
    child = zhash_first (cf->children);
    while (child) {
        flux_future_set_reactor (child, r);
        if (flux_future_then (child, -1., child_cb, (void *) f) < 0)
            goto error;
        child = zhash_next (cf->children);
    }
    return;
error:
    flux_future_fulfill_error (f, errno);
}

/*
 *  Construct a composite future.
 *  If the wait_any flag is 1 then make this a "wait any" composite.
 */
static flux_future_t *future_create_composite (int wait_any)
{
    struct composite_future *cf = composite_future_create ();
    flux_future_t *f = flux_future_create (composite_future_init, (void *) cf);
    if (!f || !cf || flux_future_aux_set (f, "flux::composite", cf,
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

    if (!f || !name || !child || !(cf = composite_get (f))) {
        errno = EINVAL;
        return (-1);
    }
    if (zhash_insert (cf->children, name, child) < 0)
        return (-1);
    zhash_freefn (cf->children, name, (flux_free_f) flux_future_destroy);
    if (flux_future_aux_set (child, "flux::parent", f, NULL) < 0) {
        zhash_delete (cf->children, name);
        return (-1);
    }
    return (0);
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
