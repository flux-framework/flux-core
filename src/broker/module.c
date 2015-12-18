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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <json.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

#include "heartbeat.h"
#include "module.h"
#include "modservice.h"

#define MODULE_MAGIC    0xfeefbe01
struct module_struct {
    int magic;

    zctx_t *zctx;
    uint32_t rank;
    flux_t broker_h;
    flux_watcher_t *broker_w;

    int lastseen;
    heartbeat_t *heartbeat;

    void *sock;             /* broker end of PAIR socket */

    zuuid_t *uuid;          /* uuid for unique request sender identity */
    pthread_t t;            /* module thread */
    mod_main_f *main;       /* dlopened mod_main() */
    char *name;
    char *service;
    void *dso;              /* reference on dlopened module */
    int size;               /* size of .so file for lsmod */
    char *digest;           /* digest of .so file for lsmod */
    size_t argz_len;
    char *argz;

    modpoller_cb_f poller_cb;
    void *poller_arg;
    rmmod_cb_f rmmod_cb;
    void *rmmod_arg;

    zlist_t *rmmod;

    flux_t h;               /* module's handle */

    zlist_t *subs;          /* subscription strings */
};

struct modhash_struct {
    zhash_t *zh_byuuid;
    zctx_t *zctx;
    uint32_t rank;
    flux_t broker_h;
    heartbeat_t *heartbeat;
};

static void *module_thread (void *arg)
{
    module_t *p = arg;
    assert (p->magic == MODULE_MAGIC);
    sigset_t signal_set;
    int errnum;
    char *uri = xasprintf ("shmem://%s", zuuid_str (p->uuid));
    char **av = NULL;
    char *rankstr = NULL;
    int ac;

    assert (p->zctx);

    /* Connect to broker socket, enable logging, register built-in services
     */
    if (!(p->h = flux_open (uri, 0)))
        err_exit ("flux_open %s", uri);
    if (flux_opt_set (p->h, FLUX_OPT_ZEROMQ_CONTEXT,
                      p->zctx, sizeof (p->zctx)) < 0)
        err_exit ("flux_opt_set ZEROMQ_CONTEXT");

    rankstr = xasprintf ("%u", p->rank);
    if (flux_attr_fake (p->h, "rank", rankstr, FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        err ("%s: error faking rank attribute", p->name);
        goto done;
    }
    flux_log_set_facility (p->h, p->name);
    modservice_register (p->h, p);

    /* Block all signals
     */
    if (sigfillset (&signal_set) < 0)
        err_exit ("%s: sigfillset", p->name);
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0)
        errn_exit (errnum, "pthread_sigmask");

    /* Run the module's main().
     */
    ac = argz_count (p->argz, p->argz_len);
    av = xzmalloc (sizeof (av[0]) * (ac + 1));
    argz_extract (p->argz, p->argz_len, av);
    if (p->main(p->h, ac, av) < 0) {
        err ("%s: mod_main returned error", p->name);
        goto done;
    }
done:
    if (rankstr)
        free (rankstr);
    if (av)
        free (av);
    flux_close (p->h);
    p->h = NULL;
    return NULL;
}

const char *module_get_name (module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    return p->name;
}

const char *module_get_service (module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    return p->service;
}

const char *module_get_uuid (module_t *p)
{
    return zuuid_str (p->uuid);
}

static int module_get_idle (module_t *p)
{
    return heartbeat_get_epoch (p->heartbeat) - p->lastseen;
}

flux_msg_t *module_recvmsg (module_t *p)
{
    flux_msg_t *msg = NULL;
    int type;
    assert (p->magic == MODULE_MAGIC);

    if (!(msg = flux_msg_recvzsock (p->sock)))
        goto error;
    if (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE) {
        if (flux_msg_pop_route (msg, NULL) < 0) /* simulate DEALER socket */
            goto error;
    }
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int module_sendmsg (module_t *p, const flux_msg_t *msg)
{
    flux_msg_t *cpy = NULL;
    int type;
    int rc = -1;

    if (!msg)
        return 0;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST: { /* simulate DEALER socket */
            char uuid[16];
            snprintf (uuid, sizeof (uuid), "%u", p->rank);
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            if (flux_msg_push_route (cpy, uuid) < 0)
                goto done;
            if (flux_msg_sendzsock (p->sock, cpy) < 0)
                goto done;
            break;
        }
        case FLUX_MSGTYPE_RESPONSE: { /* simulate ROUTER socket */
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            if (flux_msg_pop_route (cpy, NULL) < 0)
                goto done;
            if (flux_msg_sendzsock (p->sock, cpy) < 0)
                goto done;
            break;
        }
        default:
            if (flux_msg_sendzsock (p->sock, msg) < 0)
                goto done;
            break;
    }
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

int module_response_sendmsg (modhash_t *mh, const flux_msg_t *msg)
{
    char *uuid = NULL;
    int rc = -1;
    module_t *p;

    if (!msg)
        return 0;
    if (flux_msg_get_route_last (msg, &uuid) < 0)
        goto done;
    if (!uuid) {
        errno = EPROTO;
        goto done;
    }
    if (!(p = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOSYS;
        goto done;
    }
    rc = module_sendmsg (p, msg);
done:
    if (uuid)
        free (uuid);
    return rc;
}

static void module_destroy (module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    int errnum;

    if (p->t) {
        errnum = pthread_join (p->t, NULL);
        if (errnum)
            errn_exit (errnum, "pthread_join");
    }

    assert (p->h == NULL);

    flux_watcher_stop (p->broker_w);
    flux_watcher_destroy (p->broker_w);
    zsocket_destroy (p->zctx, p->sock);

    dlclose (p->dso);
    zuuid_destroy (&p->uuid);
    free (p->digest);
    if (p->argz)
        free (p->argz);
    if (p->name)
        free (p->name);
    if (p->service)
        free (p->service);
    if (p->rmmod_cb)
        p->rmmod_cb (p, p->rmmod_arg);
    if (p->rmmod) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (p->rmmod)))
            flux_msg_destroy (msg);
    }
    if (p->subs) {
        char *s;
        while ((s = zlist_pop (p->subs)))
            free (s);
        zlist_destroy (&p->subs);
    }
    zlist_destroy (&p->rmmod);
    p->magic = ~MODULE_MAGIC;
    free (p);
}

/* Send shutdown request, broker to module.
 */
int module_stop (module_t *p, const flux_msg_t *rmmod)
{
    assert (p->magic == MODULE_MAGIC);
    char *topic = xasprintf ("%s.shutdown", p->name);
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto done;
    if (flux_msg_sendzsock (p->sock, msg) < 0)
        goto done;
    if (rmmod) {
        flux_msg_t *cpy = flux_msg_copy (rmmod, true);
        if (!cpy || zlist_append (p->rmmod, cpy) < 0)
            oom ();
    }
    rc = 0;
done:
    free (topic);
    flux_msg_destroy (msg);
    return rc;
}

static void module_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    module_t *p = arg;
    assert (p->magic == MODULE_MAGIC);
    p->lastseen = heartbeat_get_epoch (p->heartbeat);
    if (p->poller_cb)
        p->poller_cb (p, p->poller_arg);
}

int module_start (module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    int errnum;
    int rc = -1;

    flux_watcher_start (p->broker_w);
    if ((errnum = pthread_create (&p->t, NULL, module_thread, p))) {
        errno = errnum;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void module_set_args (module_t *p, int argc, char * const argv[])
{
    int e;

    assert (p->magic == MODULE_MAGIC);
    if (p->argz) {
        free (p->argz);
        p->argz_len = 0;
    }
    if (argv && (e = argz_create (argv, &p->argz, &p->argz_len)) != 0)
        errn_exit (e, "argz_create");
}

void module_add_arg (module_t *p, const char *arg)
{
    int e;

    assert (p->magic == MODULE_MAGIC);
    if ((e = argz_add (&p->argz, &p->argz_len, arg)) != 0)
        errn_exit (e, "argz_add");
}

void module_set_poller_cb (module_t *p, modpoller_cb_f cb, void *arg)
{
    assert (p->magic == MODULE_MAGIC);
    p->poller_cb = cb;
    p->poller_arg = arg;
}

void module_set_rmmod_cb (module_t *p, rmmod_cb_f cb, void *arg)
{
    assert (p->magic == MODULE_MAGIC);
    p->rmmod_cb = cb;
    p->rmmod_arg = arg;
}

flux_msg_t *module_pop_rmmod (module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    return zlist_pop (p->rmmod);
}

module_t *module_add (modhash_t *mh, const char *path)
{
    module_t *p;
    void *dso;
    const char **mod_namep;
    const char **mod_servicep;
    mod_main_f *mod_main;
    zfile_t *zf;
    int rc;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_LOCAL))) {
        msg ("%s", dlerror ());
        errno = ENOENT;
        return NULL;
    }
    mod_main = dlsym (dso, "mod_main");
    mod_namep = dlsym (dso, "mod_name");
    mod_servicep = dlsym (dso, "mod_service");
    if (!mod_main || !mod_namep || !*mod_namep) {
        dlclose (dso);
        errno = ENOENT;
        return NULL;
    }
    p = xzmalloc (sizeof (*p));
    p->name = xstrdup (*mod_namep);
    if (mod_servicep && *mod_servicep)
        p->service = xstrdup (*mod_servicep);
    p->magic = MODULE_MAGIC;
    p->main = mod_main;
    p->dso = dso;
    zf = zfile_new (NULL, path);
    p->digest = xstrdup (zfile_digest (zf));
    p->size = (int)zfile_cursize (zf);
    zfile_destroy (&zf);
    if (!(p->uuid = zuuid_new ()))
        oom ();
    if (!(p->rmmod = zlist_new ()))
        oom ();
    if (!(p->subs = zlist_new ()))
        oom ();

    p->rank = mh->rank;
    p->zctx = mh->zctx;
    p->broker_h = mh->broker_h;
    p->heartbeat = mh->heartbeat;

    /* Broker end of PAIR socket is opened here.
     */
    if (!(p->sock = zsocket_new (p->zctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->sock, 0);
    if (zsocket_bind (p->sock, "inproc://%s", module_get_uuid (p)) < 0)
        err_exit ("zsock_bind inproc://%s", module_get_uuid (p));
    if (!(p->broker_w = flux_zmq_watcher_create (flux_get_reactor (p->broker_h),
                                                 p->sock, FLUX_POLLIN,
                                                 module_cb, p)))
        err_exit ("flux_zmq_watcher_create");

    /* Update the modhash.
     */
    rc = zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    assert (rc == 0); /* uuids are by definition unique */
    zhash_freefn (mh->zh_byuuid, module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
    return p;
}

void module_remove (modhash_t *mh, module_t *p)
{
    assert (p->magic == MODULE_MAGIC);
    zhash_delete (mh->zh_byuuid, module_get_uuid (p));
}

modhash_t *modhash_create (void)
{
    modhash_t *mh = xzmalloc (sizeof (*mh));
    if (!(mh->zh_byuuid = zhash_new ()))
        oom ();
    return mh;
}

void modhash_destroy (modhash_t *mh)
{
    if (mh) {
        zhash_destroy (&mh->zh_byuuid);
        free (mh);
    }
}

void modhash_set_zctx (modhash_t *mh, zctx_t *zctx)
{
    mh->zctx = zctx;
}

void modhash_set_rank (modhash_t *mh, uint32_t rank)
{
    mh->rank = rank;
}

void modhash_set_flux (modhash_t *mh, flux_t h)
{
    mh->broker_h = h;
}

void modhash_set_heartbeat (modhash_t *mh, heartbeat_t *hb)
{
    mh->heartbeat = hb;
}

flux_modlist_t module_get_modlist (modhash_t *mh)
{
    flux_modlist_t mods;
    zlist_t *uuids;
    char *uuid;
    module_t *p;

    if (!(mods = flux_modlist_create ()))
        goto done;
    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (flux_modlist_append (mods, module_get_name (p), p->size,
                                 p->digest, module_get_idle (p)) < 0) {
            flux_modlist_destroy (mods);
            mods = NULL;
            goto done;
        }
        uuid = zlist_next (uuids);
    }
done:
    zlist_destroy (&uuids);
    return mods;
}

int module_stop_all (modhash_t *mh)
{
    zlist_t *uuids;
    char *uuid;
    int rc = -1;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (module_stop (p, NULL) < 0)
            goto done;
        uuid = zlist_next (uuids);
    }
    rc = 0;
done:
    zlist_destroy (&uuids);
    return rc;
}

int module_start_all (modhash_t *mh)
{
    zlist_t *uuids;
    char *uuid;
    int rc = -1;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (module_start (p) < 0)
            goto done;
        uuid = zlist_next (uuids);
    }
    rc = 0;
done:
    zlist_destroy (&uuids);
    return rc;
}

module_t *module_lookup_byname (modhash_t *mh, const char *name)
{
    zlist_t *uuids;
    char *uuid;
    module_t *result = NULL;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (!strcmp (module_get_name (p), name)) {
            result = p;
            break;
        }
        uuid = zlist_next (uuids);
        p = NULL;
    }
    zlist_destroy (&uuids);
    return result;
}

int module_subscribe (modhash_t *mh, const char *uuid, const char *topic)
{
    module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
    int rc = -1;

    if (!p) {
        errno = ENOENT;
        goto done;
    }
    if (zlist_push (p->subs, xstrdup (topic)) < 0)
        oom ();
    rc = 0;
done:
    return rc;
}

int module_unsubscribe (modhash_t *mh, const char *uuid, const char *topic)
{
    module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
    char *s;
    int rc = -1;

    if (!p) {
        errno = ENOENT;
        goto done;
    }
    s = zlist_first (p->subs);
    while (s) {
        if (!strcmp (topic, s)) {
            zlist_remove (p->subs, s);
            break;
        }
        s = zlist_next (p->subs);
    }
    rc = 0;
done:
    return rc;
}

static bool match_sub (module_t *p, const char *topic)
{
    char *s = zlist_first (p->subs);

    while (s) {
        if (!strncmp (topic, s, strlen (s)))
            return true;
        s = zlist_next (p->subs);
    }
    return false;
}

int module_event_mcast (modhash_t *mh, const flux_msg_t *msg)
{
    const char *topic;
    module_t *p;
    int rc = -1;

    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;
    p = zhash_first (mh->zh_byuuid);
    while (p) {
        if (match_sub (p, topic)) {
            if (module_sendmsg (p, msg) < 0)
                goto done;
        }
        p = zhash_next (mh->zh_byuuid);
    }
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
