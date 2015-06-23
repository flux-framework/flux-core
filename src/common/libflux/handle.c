/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <czmq.h>

#include "handle.h"
#include "reactor.h"
#include "reactor_impl.h"
#include "connector.h"
#include "message.h"
#include "tagpool.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"


struct flux_handle_struct {
    const struct flux_handle_ops *ops;
    int             flags;
    void            *impl;
    void            *dso;
    zhash_t         *aux;
    tagpool_t       tagpool;
    struct reactor  *reactor;
    flux_msgcounters_t msgcounters;
    flux_fatal_f    fatal;
    void            *fatal_arg;
    bool            fatal_called;
};

static char *find_file_r (const char *name, const char *dirpath)
{
    DIR *dir;
    struct dirent entry, *dent;
    char *filepath = NULL;
    struct stat sb;
    char path[PATH_MAX];
    size_t len = sizeof (path);

    if (!(dir = opendir (dirpath)))
        goto done;
    while (!filepath) {
        if ((errno = readdir_r (dir, &entry, &dent)) > 0 || dent == NULL)
            break;
        if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
            continue;
        if (snprintf (path, len, "%s/%s", dirpath, dent->d_name) >= len) {
            errno = EINVAL;
            break;
        }
        if (stat (path, &sb) == 0) {
            if (S_ISDIR (sb.st_mode))
                filepath = find_file_r (name, path);
            else if (!strcmp (dent->d_name, name)) {
                if (!(filepath = realpath (path, NULL)))
                    oom ();
            }
        }
    }
    closedir (dir);
done:
    return filepath;
}

static char *find_file (const char *name, const char *searchpath)
{
    char *cpy = xstrdup (searchpath);
    char *dirpath, *saveptr = NULL, *a1 = cpy;
    char *path = NULL;

    while ((dirpath = strtok_r (a1, ":", &saveptr))) {
        if ((path = find_file_r (name, dirpath)))
            break;
        a1 = NULL;
    }
    free (cpy);
    return path;
}

static connector_init_f *find_connector (const char *scheme, void **dsop)
{
    char *name = xasprintf ("%s.so", scheme);
    const char *searchpath = getenv ("FLUX_CONNECTOR_PATH");
    char *path = NULL;
    void *dso = NULL;
    connector_init_f *connector_init = NULL;

    if (!searchpath)
        searchpath = CONNECTOR_PATH;
    if (!(path = find_file (name, searchpath))) {
        errno = ENOENT;
        goto done;
    }
    if (!(dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL))) {
        errno = EINVAL;
        goto done;
    }
    if (!(connector_init = dlsym (dso, "connector_init"))) {
        dlclose (dso);
        errno = EINVAL;
        goto done;
    }
    *dsop = dso;
done:
    if (path)
        free (path);
    free (name);
    return connector_init;
}

static char *strtrim (char *s, const char *trim)
{
    char *p = s + strlen (s) - 1;
    while (p >= s && strchr (trim, *p))
        *p-- = '\0';
    return *s ? s : NULL;
}

flux_t flux_open (const char *uri, int flags)
{
    const char *scheme = "local";
    char *path = NULL;
    char *cpy = NULL;
    void *dso = NULL;
    connector_init_f *connector_init = NULL;
    flux_t h = NULL;

    if (!uri)
        uri = getenv ("FLUX_URI");
    if (getenv ("FLUX_HANDLE_TRACE"))
        flags |= FLUX_O_TRACE;
    if (uri) {
        cpy = xstrdup (uri);
        path = strstr (cpy, "://");
        if (path) {
            *path = '\0';
            path = strtrim (path + 3, " \t");
        }
        scheme = cpy;
    }
    if (!(connector_init = find_connector (scheme, &dso)))
        goto done;
    if (!(h = connector_init (path, flags))) {
        dlclose (dso);
        goto done;
    }
    h->dso = dso;
done:
    if (cpy)
        free (cpy);
    return h;
}

void flux_close (flux_t h)
{
    flux_handle_destroy (&h);
}

flux_t flux_handle_create (void *impl, const struct flux_handle_ops *ops, int flags)
{
    flux_t h = xzmalloc (sizeof (*h));

    h->flags = flags;
    if (!(h->aux = zhash_new()))
        oom ();
    h->ops = ops;
    h->impl = impl;
    h->tagpool = tagpool_create ();
    h->reactor = reactor_create (impl, ops);
    return h;
}

void flux_handle_destroy (flux_t *hp)
{
    flux_t h;

    if (hp && (h = *hp)) {
        zhash_destroy (&h->aux);
        if (h->ops->impl_destroy)
            h->ops->impl_destroy (h->impl);
        tagpool_destroy (h->tagpool);
        reactor_destroy (h->reactor);
        if (h->dso)
            dlclose (h->dso);
        free (h);
        *hp = NULL;
    }
}

void flux_flags_set (flux_t h, int flags)
{
    h->flags |= flags;
}

void flux_flags_unset (flux_t h, int flags)
{
    h->flags &= ~flags;
}

int flux_flags_get (flux_t h)
{
    return h->flags;
}

void *flux_aux_get (flux_t h, const char *name)
{
    return zhash_lookup (h->aux, name);
}

void flux_aux_set (flux_t h, const char *name, void *aux, flux_free_f destroy)
{
    zhash_update (h->aux, name, aux);
    zhash_freefn (h->aux, name, destroy);
}

void flux_fatal_set (flux_t h, flux_fatal_f fun, void *arg)
{
    h->fatal = fun;
    h->fatal_arg = arg;
    h->fatal_called = false;
}

void flux_fatal_error (flux_t h, const char *fun, const char *msg)
{
    if (h->fatal && !h->fatal_called) {
        char buf[256];
        snprintf (buf, sizeof (buf), "%s: %s", fun, msg);
        h->fatal_called = true;
        h->fatal (buf, h->fatal_arg);
    }
}

void flux_get_msgcounters (flux_t h, flux_msgcounters_t *mcs)
{
    *mcs = h->msgcounters;
}

void flux_clr_msgcounters (flux_t h)
{
    memset (&h->msgcounters, 0, sizeof (h->msgcounters));
}

uint32_t flux_matchtag_alloc (flux_t h, int len)
{
    return tagpool_alloc (h->tagpool, len);
}

void flux_matchtag_free (flux_t h, uint32_t matchtag, int len)
{
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .topic_glob = NULL,
        .matchtag = matchtag,
        .bsize = len,
    };
    if (h->ops->purge)
        h->ops->purge (h->impl, match);
    tagpool_free (h->tagpool, matchtag, len);
}

uint32_t flux_matchtag_avail (flux_t h)
{
    return tagpool_avail (h->tagpool);
}

static void update_tx_stats (flux_t h, const flux_msg_t *msg)
{
    int type;
    if (flux_msg_get_type (msg, &type) == 0) {
        switch (type) {
            case FLUX_MSGTYPE_REQUEST:
                h->msgcounters.request_tx++;
                break;
            case FLUX_MSGTYPE_RESPONSE:
                h->msgcounters.response_tx++;
                break;
            case FLUX_MSGTYPE_EVENT:
                h->msgcounters.event_tx++;
                break;
            case FLUX_MSGTYPE_KEEPALIVE:
                h->msgcounters.keepalive_tx++;
                break;
        }
    } else
        errno = 0;
}

static void update_rx_stats (flux_t h, const flux_msg_t *msg)
{
    int type;
    if (flux_msg_get_type (msg, &type) == 0) {
        switch (type) {
            case FLUX_MSGTYPE_REQUEST:
                h->msgcounters.request_rx++;
                break;
            case FLUX_MSGTYPE_RESPONSE:
                h->msgcounters.response_rx++;
                break;
            case FLUX_MSGTYPE_EVENT:
                h->msgcounters.event_rx++;
                break;
        case FLUX_MSGTYPE_KEEPALIVE:
            h->msgcounters.keepalive_rx++;
            break;
        }
    } else
        errno = 0;
}

int flux_send (flux_t h, const flux_msg_t *msg, int flags)
{
    if (!h->ops->send) {
        errno = ENOSYS;
        goto fatal;
    }
    flags |= h->flags;
    update_tx_stats (h, msg);
    if (flags & FLUX_O_TRACE)
        flux_msg_fprint (stderr, msg);
    if (h->ops->send (h->impl, msg, flags) < 0)
        goto fatal;
    return 0;
fatal:
    FLUX_FATAL (h);
    return -1;
}

static int defer_enqueue (zlist_t **l, flux_msg_t *msg)
{
    if ((!*l && !(*l = zlist_new ())) || zlist_append (*l, msg) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int defer_requeue (zlist_t **l, flux_t h)
{
    flux_msg_t *msg;
    if (*l) {
        while ((msg = zlist_pop (*l))) {
            int rc = flux_requeue (h, msg, FLUX_RQ_TAIL);
            flux_msg_destroy (msg);
            if (rc < 0)
                return -1;
        }
    }
    return 0;
}

static void defer_destroy (zlist_t **l)
{
    flux_msg_t *msg;
    if (*l) {
        while ((msg = zlist_pop (*l)))
            flux_msg_destroy (msg);
        zlist_destroy (l);
    }
}

/* If this function is called without the NONBLOCK flag from a reactor
 * handler running in coprocess context, the call to flux_sleep_on()
 * will allow the reactor to run until a message matching 'match' arrives.
 * The flux_sleep_on() call will then resume, and the next call to recv()
 * will return the matching message.  If not running in coprocess context,
 * flux_sleep_on() will fail with EINVAL.  In that case, the do loop
 * reading messages and comparing them to match criteria may have to read
 * a few non-matching messages before finding a match.  On return, those
 * non-matching messages have to be requeued in the handle, hence the
 * defer_*() helper calls.
 */
flux_msg_t *flux_recv (flux_t h, struct flux_match match, int flags)
{
    zlist_t *l = NULL;
    flux_msg_t *msg = NULL;
    int saved_errno;

    if (!h->ops->recv || !h->ops->requeue) {
        errno = ENOSYS;
        goto fatal;
    }
    flags |= h->flags;
    if (!(flags & FLUX_O_NONBLOCK) && flux_sleep_on (h, match) < 0) {
        if (errno != EINVAL)
            goto fatal;
        errno = 0;
    }
    do {
        if (!(msg = h->ops->recv (h->impl, flags))) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                goto fatal;
            if (defer_requeue (&l, h) < 0)
                goto fatal;
            defer_destroy (&l);
            errno = EWOULDBLOCK;
            return NULL;
        }
        if (!flux_msg_cmp (msg, match)) {
            if (defer_enqueue (&l, msg) < 0)
                goto fatal;
            msg = NULL;
        }
    } while (!msg);
    update_rx_stats (h, msg);
    if ((flags & FLUX_O_TRACE))
        flux_msg_fprint (stderr, msg);
    if (defer_requeue (&l, h) < 0)
        goto fatal;
    defer_destroy (&l);
    return msg;
fatal:
    saved_errno = errno;
    FLUX_FATAL (h);
    if (msg)
        flux_msg_destroy (msg);
    defer_destroy (&l);
    errno = saved_errno;
    return NULL;
}

/* FIXME: FLUX_O_TRACE will show these messages being received again
 */
int flux_requeue (flux_t h, const flux_msg_t *msg, int flags)
{
    if (!h->ops->requeue) {
        errno = ENOSYS;
        goto fatal;
    }
    if (h->ops->requeue(h->impl, msg, flags) < 0)
        goto fatal;
    return 0;
fatal:
    FLUX_FATAL (h);
    return -1;
}

int flux_event_subscribe (flux_t h, const char *topic)
{
    if (h->ops->event_subscribe) {
        if (h->ops->event_subscribe (h->impl, topic) < 0)
            goto fatal;
    }
    return 0;
fatal:
    FLUX_FATAL (h);
    return -1;
}

int flux_event_unsubscribe (flux_t h, const char *topic)
{
    if (h->ops->event_unsubscribe) {
        if (h->ops->event_unsubscribe (h->impl, topic) < 0)
            goto fatal;
    }
    return 0;
fatal:
    FLUX_FATAL (h);
    return -1;
}

int flux_rank (flux_t h)
{
    int rank;
    if (!h->ops->rank) {
        errno = ENOSYS;
        goto fatal;
    }
    if ((rank = h->ops->rank (h->impl)) < 0)
        goto fatal;
    return rank;
fatal:
    FLUX_FATAL (h);
    return -1;
}

zctx_t *flux_get_zctx (flux_t h)
{
    if (!h->ops->get_zctx) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_zctx (h->impl);
}

struct reactor *reactor_get (flux_t h)
{
    return h->reactor;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
