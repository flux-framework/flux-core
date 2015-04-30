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
#include "modhandle.h"
#include "modservice.h"

/* While transitioning to argc, argv - style args per RFC 5,
 * we have our own mod_main prototype.
 */
typedef int (mod_main_comms_f)(flux_t h, zhash_t *args);

#define MODULE_MAGIC    0xfeefbe01
struct module_struct {
    int magic;

    zctx_t *zctx;
    uint32_t rank;
    zloop_t *zloop;
    zmq_pollitem_t zp;

    int lastseen;
    heartbeat_t heartbeat;

    void *zs_svc[2];        /* PAIR for handling requests 0=module, 1=broker */

    char *svc_uri;

    zuuid_t *uuid;          /* uuid for unique request sender identity */
    pthread_t t;            /* module thread */
    mod_main_comms_f *main; /* dlopened mod_main() */
    char *name;
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
    zloop_t *zloop;
    heartbeat_t heartbeat;
};

/* FIXME: this should go away once module prototypes are updated.
 */
static zhash_t *zhash_fromargz (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    zhash_t *z;
    char *arg;

    if (!(z = zhash_new ()))
        oom ();
    arg = argz_next (p->argz, p->argz_len, NULL);
    while (arg != NULL) {
        char *key = xstrdup (arg);
        char *val = strchr (key, '=');
        if (!val || strlen (val) == 0)
            val = "1";
        else
            *val++ = '\0';
        zhash_update (z, key, xstrdup (val));
        zhash_freefn (z, key, (zhash_free_fn *)free);
        free (key);
        arg = argz_next (p->argz, p->argz_len, arg);
    }
    return z;
}

static void send_eof (void *sock)
{
    zmsg_t *zmsg;

    if (!(zmsg = zmsg_new ()) || zmsg_pushmem (zmsg, NULL, 0) < 0)
        oom ();
    if (zmsg_send (&zmsg, sock) < 0)
        err_exit ("error sending EOF");
    zmsg_destroy (&zmsg);
}

static void *module_thread (void *arg)
{
    module_t p = arg;
    assert (p->magic == MODULE_MAGIC);
    sigset_t signal_set;
    int errnum;
    zhash_t *args = NULL;

    assert (p->zctx);

    /* Connect to broker socket
     */
    if (!(p->zs_svc[0] = zsocket_new (p->zctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->zs_svc[0], 0);
    if (zsocket_connect (p->zs_svc[0], "%s", p->svc_uri) < 0)
        err_exit ("%s", p->svc_uri);

    /* Block all signals
     */
    if (sigfillset (&signal_set) < 0)
        err_exit ("%s: sigfillset", p->name);
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0)
        errn_exit (errnum, "pthread_sigmask");

    /* Create handle, enable logging, register built-in services
     */
    p->h = modhandle_create (p->zs_svc[0], zuuid_str (p->uuid),
                             p->rank, p->zctx);
    flux_log_set_facility (p->h, p->name);
    modservice_register (p->h, p->name);

    /* Run the module's main().
     */
    args = zhash_fromargz (p);
    if (p->main(p->h, args) < 0) {
        err ("%s: mod_main returned error", p->name);
        goto done;
    }
done:
    zhash_destroy (&args);

    send_eof (p->zs_svc[0]);

    zsocket_destroy (p->zctx, p->zs_svc[0]);

    return NULL;
}

const char *module_get_name (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    return p->name;
}

void module_set_name (module_t p, const char *name)
{
    assert (p->magic == MODULE_MAGIC);
    if (p->name)
        free (p->name);
    p->name = xstrdup (name);
}

static const char *module_get_uuid (module_t p)
{
    return zuuid_str (p->uuid);
}

static int module_get_idle (module_t p)
{
    return heartbeat_get_epoch (p->heartbeat) - p->lastseen;
}

zmsg_t *module_recvmsg (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    return zmsg_recv (p->zs_svc[1]);
}

int module_sendmsg (zmsg_t **zmsg, module_t p)
{
    if (!zmsg || !*zmsg)
        return 0;
    return zmsg_send (zmsg, p->zs_svc[1]);
}

int module_response_sendmsg (modhash_t mh, zmsg_t **zmsg)
{
    char *uuid = NULL;
    int rc = -1;
    module_t p;

    if (!zmsg || !*zmsg)
        return 0;
    if (flux_msg_get_route_last (*zmsg, &uuid) < 0)
        goto done;
    if (!(p = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOSYS;
        goto done;
    }
    free (uuid);
    (void)flux_msg_pop_route (*zmsg, &uuid);
    rc = module_sendmsg (zmsg, p);
done:
    if (uuid)
        free (uuid);
    return rc;
}

static void module_destroy (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    int errnum;

    if (p->t) {
        errnum = pthread_join (p->t, NULL);
        if (errnum)
            errn_exit (errnum, "pthread_join");
    }

    flux_handle_destroy (&p->h);

    zloop_poller_end (p->zloop, &p->zp);
    zsocket_destroy (p->zctx, p->zs_svc[1]);

    dlclose (p->dso);
    zuuid_destroy (&p->uuid);
    free (p->digest);
    if (p->argz)
        free (p->argz);
    if (p->svc_uri)
        free (p->svc_uri);
    if (p->name)
        free (p->name);
    if (p->rmmod_cb)
        p->rmmod_cb (p, p->rmmod_arg);
    if (p->rmmod) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (p->rmmod)))
            zmsg_destroy (&zmsg);
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
int module_stop (module_t p, zmsg_t **rmmod)
{
    assert (p->magic == MODULE_MAGIC);
    char *topic = xasprintf ("%s.shutdown", p->name);
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (zmsg_send (&zmsg, p->zs_svc[1]) < 0)
        goto done;
    if (rmmod) {
        if (zlist_append (p->rmmod, *rmmod) < 0)
            oom ();
        *rmmod = NULL;
    }
    rc = 0;
done:
    free (topic);
    zmsg_destroy (&zmsg);
    return rc;
}

static int module_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    module_t p = arg;
    assert (p->magic == MODULE_MAGIC);
    p->lastseen = heartbeat_get_epoch (p->heartbeat);
    if (p->poller_cb)
        p->poller_cb (p, p->poller_arg);
    return 0;
}

int module_start (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    int errnum;
    int rc = -1;

    if (zloop_poller (p->zloop, &p->zp, module_cb, p) < 0)
        goto done;
    if ((errnum = pthread_create (&p->t, NULL, module_thread, p))) {
        errno = errnum;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void module_set_args (module_t p, int argc, char * const argv[])
{
    assert (p->magic == MODULE_MAGIC);
    if (p->argz) {
        free (p->argz);
        p->argz_len = 0;
    }
    if (argv && argz_create (argv, &p->argz, &p->argz_len) < 0)
        oom ();
}

void module_add_arg (module_t p, const char *arg)
{
    assert (p->magic == MODULE_MAGIC);
    if (argz_add (&p->argz, &p->argz_len, arg) < 0)
        oom ();
}

void module_set_poller_cb (module_t p, modpoller_cb_f cb, void *arg)
{
    assert (p->magic == MODULE_MAGIC);
    p->poller_cb = cb;
    p->poller_arg = arg;
}

void module_set_rmmod_cb (module_t p, rmmod_cb_f cb, void *arg)
{
    assert (p->magic == MODULE_MAGIC);
    p->rmmod_cb = cb;
    p->rmmod_arg = arg;
}

zmsg_t *module_pop_rmmod (module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    return zlist_pop (p->rmmod);
}

module_t module_add (modhash_t mh, const char *path)
{
    module_t p;
    void *dso;
    const char **mod_namep;
    mod_main_comms_f *mod_main;
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
    if (!mod_main || !mod_namep || !*mod_namep) {
        dlclose (dso);
        errno = ENOENT;
        return NULL;
    }
    p = xzmalloc (sizeof (*p));
    p->name = xstrdup (*mod_namep);
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
    p->zloop = mh->zloop;
    p->heartbeat = mh->heartbeat;

    /* Module end socket will be opened in the module thread.
     */
    p->svc_uri = xasprintf ("inproc:///%s", module_get_uuid (p));

    /* Broker end socket is opened here.
     */
    if (!(p->zs_svc[1] = zsocket_new (p->zctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->zs_svc[1], 0);
    if (zsocket_bind (p->zs_svc[1], "%s", p->svc_uri) < 0)
        err_exit ("%s", p->svc_uri);
    p->zp.events = ZMQ_POLLIN;
    p->zp.socket = p->zs_svc[1];

    /* Update the modhash.
     */
    rc = zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    assert (rc == 0); /* uuids are by definition unique */
    zhash_freefn (mh->zh_byuuid, module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
    return p;
}

void module_remove (modhash_t mh, module_t p)
{
    assert (p->magic == MODULE_MAGIC);
    zhash_delete (mh->zh_byuuid, module_get_uuid (p));
}

modhash_t modhash_create (void)
{
    modhash_t mh = xzmalloc (sizeof (*mh));
    if (!(mh->zh_byuuid = zhash_new ()))
        oom ();
    return mh;
}

void modhash_destroy (modhash_t mh)
{
    if (mh) {
        zhash_destroy (&mh->zh_byuuid);
        free (mh);
    }
}

void modhash_set_zctx (modhash_t mh, zctx_t *zctx)
{
    mh->zctx = zctx;
}

void modhash_set_rank (modhash_t mh, uint32_t rank)
{
    mh->rank = rank;
}

void modhash_set_loop (modhash_t mh, zloop_t *zloop)
{
    mh->zloop = zloop;
}

void modhash_set_heartbeat (modhash_t mh, heartbeat_t hb)
{
    mh->heartbeat = hb;
}

JSON module_list_encode (modhash_t mh)
{
    JSON out = flux_lsmod_json_create ();
    zlist_t *uuids;
    char *uuid;
    module_t p;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (flux_lsmod_json_append (out, module_get_name (p), p->size,
                                    p->digest, module_get_idle (p)) < 0) {
            Jput (out);
            out = NULL;
            goto done;
        }
        uuid = zlist_next (uuids);
    }
done:
    zlist_destroy (&uuids);
    return out;
}

int module_stop_all (modhash_t mh)
{
    zlist_t *uuids;
    char *uuid;
    int rc = -1;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t p = zhash_lookup (mh->zh_byuuid, uuid);
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

int module_start_all (modhash_t mh)
{
    zlist_t *uuids;
    char *uuid;
    int rc = -1;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t p = zhash_lookup (mh->zh_byuuid, uuid);
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

module_t module_lookup_byname (modhash_t mh, const char *name)
{
    zlist_t *uuids;
    char *uuid;
    module_t result = NULL;

    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t p = zhash_lookup (mh->zh_byuuid, uuid);
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

int module_subscribe (modhash_t mh, const char *uuid, const char *topic)
{
    module_t p = zhash_lookup (mh->zh_byuuid, uuid);
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

int module_unsubscribe (modhash_t mh, const char *uuid, const char *topic)
{
    module_t p = zhash_lookup (mh->zh_byuuid, uuid);
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

static bool match_sub (module_t p, const char *topic)
{
    char *s = zlist_first (p->subs);

    while (s) {
        if (!strncmp (topic, s, strlen (s)))
            return true;
        s = zlist_next (p->subs);
    }
    return false;
}

int module_event_mcast (modhash_t mh, zmsg_t *zmsg)
{
    char *topic = NULL;
    zlist_t *uuids;
    char *uuid;
    int rc = -1;

    if (flux_msg_get_topic (zmsg, &topic) < 0)
        goto done;
    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t p = zhash_lookup (mh->zh_byuuid, uuid);
        assert (p != NULL);
        if (match_sub (p, topic)) {
            zmsg_t *cpy = zmsg_dup (zmsg);
            rc = module_sendmsg (&cpy, p);
            zmsg_destroy (&cpy);
            if (rc < 0)
                goto done;
        }
        uuid = zlist_next (uuids);
    }
    rc = 0;
done:
    if (topic)
        free (topic);
    zlist_destroy (&uuids);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
