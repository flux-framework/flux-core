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

#include "handle.h"
#include "reactor.h"
#include "handle_impl.h"
#include "message.h"
#include "tagpool.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/xzmalloc.h"


struct flux_handle_struct {
    const struct flux_handle_ops *ops;
    int             flags;
    void            *impl;
    void            *dso;
    zhash_t         *aux;
    tagpool_t       tagpool;
    reactor_t       reactor;
    flux_msgcounters_t msgcounters;
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
    h->reactor = flux_reactor_create (impl, ops);
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
        flux_reactor_destroy (h->reactor);
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

void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy)
{
    zhash_update (h->aux, name, aux);
    zhash_freefn (h->aux, name, destroy);
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
    flux_match_t match = {
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

int flux_sendmsg (flux_t h, zmsg_t **zmsg)
{
    int type;

    if (!h->ops->sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (flux_msg_get_type (*zmsg, &type) < 0)
        return -1;
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
    if (h->flags & FLUX_O_TRACE)
        zdump_fprint (stderr, *zmsg, flux_msgtype_shortstr (type));
    return h->ops->sendmsg (h->impl, zmsg);
}

zmsg_t *flux_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg = NULL;
    int type;

    if (!h->ops->recvmsg) {
        errno = ENOSYS;
        goto done;
    }
    if (!(zmsg = h->ops->recvmsg (h->impl, nonblock)))
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0) {
        zmsg_destroy (&zmsg);
        errno = EPROTO;
        goto done;
    }
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
    if ((h->flags & FLUX_O_TRACE))
        zdump_fprint (stderr, zmsg, flux_msgtype_shortstr (type));
done:
    return zmsg;
}

zmsg_t *flux_recvmsg_match (flux_t h, flux_match_t match, zlist_t *nomatch,
                            bool nonblock)
{
    zmsg_t *zmsg = NULL;
    zlist_t *putmsg = nomatch;

    if (flux_sleep_on (h, match) < 0) {
        if (errno != EINVAL)
            goto done;
        errno = 0; /* EINVAL: not running in a coprocess */
    }
    while (!zmsg) {
        if (!(zmsg = flux_recvmsg (h, nonblock)))
            goto done;
        if (!flux_msg_cmp (zmsg, match)) {
            if (!putmsg && !(putmsg = zlist_new ())) {
                zmsg_destroy (&zmsg);
                errno = ENOMEM;
                goto done;
            }
            if (zlist_append (putmsg, zmsg) < 0) {
                zmsg_destroy (&zmsg);
                errno = ENOMEM;
                goto done;
            }
            zmsg = NULL;
        }
    }
done:
    if (putmsg && !nomatch) {
        if (flux_putmsg_list (h, putmsg) < 0) {
            int errnum = errno;
            zmsg_destroy (&zmsg);
            errno = errnum;
        }
        zlist_destroy (&putmsg);
    }
    return zmsg;
}

int flux_putmsg_list (flux_t h, zlist_t *l)
{
    int errnum = 0;
    int rc = 0;

    if (l) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (l))) {
            if (flux_putmsg (h, &zmsg) < 0) {
                if (errnum < errno) {
                    errnum = errno;
                    rc = -1;
                }
                zmsg_destroy (&zmsg);
            }
        }
    }
    if (errnum > 0)
        errno = errnum;
    return rc;
}

/* FIXME: FLUX_O_TRACE will show these messages being received again
 */
int flux_putmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->putmsg) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->putmsg (h->impl, zmsg);
}

int flux_pushmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->pushmsg) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->pushmsg (h->impl, zmsg);
}

int flux_event_subscribe (flux_t h, const char *topic)
{
    if (!h->ops->event_subscribe)
        return 0;
    return h->ops->event_subscribe (h->impl, topic);
}

int flux_event_unsubscribe (flux_t h, const char *topic)
{
    if (!h->ops->event_unsubscribe)
        return 0;
    return h->ops->event_unsubscribe (h->impl, topic);
}

int flux_rank (flux_t h)
{
    if (!h->ops->rank) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->rank (h->impl);
}

zctx_t *flux_get_zctx (flux_t h)
{
    if (!h->ops->get_zctx) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_zctx (h->impl);
}

reactor_t flux_get_reactor (flux_t h)
{
    return h->reactor;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
