/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <czmq.h>

#include "kvs_classic.h"
#include "kvs_classic_watch_private.h"

struct kvs_watcher {
    kvs_set_f val_cb;       // either val_cb or dir_cb is valid, not both
    kvs_set_dir_f dir_cb;
    void *arg;
    flux_future_t *f;
};

static void kvs_watcher_destroy (void **arg);
static void watchers_destroy (zhashx_t *watchesr);

/* Maintain a per-flux_t handle zhashx of kvs_watchers by key.  In this
 * implementation, there can be only one watcher per key, per handle.
 * The sole purpose of the zhashx is to allow the legacy flux_kvs_unwatch()
 * function to locate watchers by key, so they can be cancelled.
 */
static zhashx_t *watchers_get (flux_t *h)
{
    const char *auxkey = "flux::kvs_watchers";
    zhashx_t *watchers = flux_aux_get (h, auxkey);
    if (!watchers) {
        if (!(watchers = zhashx_new ())) {
            errno = ENOMEM;
            return NULL;
        }
        zhashx_set_destructor (watchers, kvs_watcher_destroy);
        if (flux_aux_set (h, auxkey, watchers,
                          (flux_free_f)watchers_destroy) < 0) {
            watchers_destroy (watchers);
            return NULL;
        }
    }
    return watchers;
}

static void watchers_destroy (zhashx_t *watchers)
{
    zhashx_destroy (&watchers);
}

/* A kvs_watcher is basically a legacy watch callback (one of two types),
 * callback argument, and a future.
 */
static void kvs_watcher_destroy (void **arg)
{
    if (arg) {
        struct kvs_watcher *w = *arg;
        if (w) {
            flux_future_destroy (w->f);
            free (w);
        }
        *arg = NULL;
    }
}

static struct kvs_watcher *kvs_watcher_create (flux_future_t *f)
{
    struct kvs_watcher *w;

    if (!(w = calloc (1, sizeof (*w))))
        return NULL;
    w->f = f;
    return w;
}

static void val_continuation (flux_future_t *f, struct kvs_watcher *w)
{
    const char *key = flux_kvs_lookup_get_key (f);
    flux_reactor_t *r = flux_future_get_reactor (f);
    const char *value = NULL;
    int errnum = 0;

    if (flux_kvs_lookup_get (f, &value) < 0)
        errnum = errno;
    else if (value == NULL)
        errnum = ENOENT;
    if (w->val_cb (key, (char *)value, w->arg, errnum) < 0)
        flux_reactor_stop_error (r);
    flux_future_reset (f);
}

static void dir_continuation (flux_future_t *f, struct kvs_watcher *w)
{
    const char *key = flux_kvs_lookup_get_key (f);
    flux_reactor_t *r = flux_future_get_reactor (f);
    const flux_kvsdir_t *dir = NULL;
    int errnum = 0;

    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        errnum = errno;
    if (w->dir_cb (key, (flux_kvsdir_t *)dir, w->arg, errnum) < 0)
        flux_reactor_stop_error (r);
    flux_future_reset (f);
}

int flux_kvs_watch (flux_t *h, const char *key, kvs_set_f set, void *arg)
{
    zhashx_t *watchers;
    struct kvs_watcher *w;
    flux_future_t *f;

    if (!h || !key || !set) {
        errno = EINVAL;
        return -1;
    }
    if (!(watchers = watchers_get (h)))
        return -1;
    if (zhashx_lookup (watchers, key)) {
        errno = EEXIST;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, CLASSIC_WATCH_FLAGS, key)))
        goto error;
    if (!(w = kvs_watcher_create (f))) {
        flux_future_destroy (f);
        goto error;
    }
    w->val_cb = set;
    w->arg = arg;
    val_continuation (f, w);
    if (flux_future_then (f, -1., (flux_continuation_f)val_continuation, w) < 0)
        goto error;
    zhashx_update (watchers, key, w);
    return 0;
error:
    kvs_watcher_destroy ((void **)&w);
    return -1;
}

static int watch_dir (flux_t *h, const char *key, kvs_set_dir_f set, void *arg)
{
    zhashx_t *watchers;
    struct kvs_watcher *w;
    flux_future_t *f;

    if (!(watchers = watchers_get (h)))
        return -1;
    if (zhashx_lookup (watchers, key)) {
        errno = EEXIST;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, CLASSIC_DIR_WATCH_FLAGS, key)))
        goto error;
    if (!(w = kvs_watcher_create (f))) {
        flux_future_destroy (f);
        goto error;
    }
    w->dir_cb = set;
    w->arg = arg;
    dir_continuation (f, w);
    if (flux_future_then (f, -1., (flux_continuation_f)dir_continuation, w) < 0)
        goto error;
    zhashx_update (watchers, key, w);
    return 0;
error:
    kvs_watcher_destroy ((void **)&w);
    return -1;
}

int flux_kvs_watch_dir (flux_t *h, kvs_set_dir_f set, void *arg,
                        const char *fmt, ...)
{
    va_list ap;
    char *key;
    int rc;

    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    rc = vasprintf (&key, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = ENOMEM;
        return -1;
    }
    rc = watch_dir (h, key, set, arg);
    free (key);
    return rc;
}

int flux_kvs_unwatch (flux_t *h, const char *key)
{
    zhashx_t *watchers;
    struct kvs_watcher *w;

    if (!h || !key) {
        errno = EINVAL;
        return -1;
    }
    if (!(watchers = watchers_get (h)))
        return -1;
    if ((w = zhashx_lookup (watchers, key))) {
        (void)kvs_cancel_streaming_lookup (w->f);
        zhashx_delete (watchers, key);
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
