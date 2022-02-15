/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <dlfcn.h>
#include <argz.h>
#include <czmq.h>
#include <uuid.h>
#include <flux/core.h>
#include <jansson.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/reactor.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/digest.h"

#include "module.h"
#include "modservice.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif


struct broker_module {
    struct modhash *modhash;

    flux_watcher_t *broker_w;

    double lastseen;

    zsock_t *sock;          /* broker end of PAIR socket */
    struct flux_msg_cred cred; /* cred of connection */

    uuid_t uuid;            /* uuid for unique request sender identity */
    char uuid_str[UUID_STR_LEN];
    pthread_t t;            /* module thread */
    mod_main_f *main;       /* dlopened mod_main() */
    char *name;
    void *dso;              /* reference on dlopened module */
    int size;               /* size of .so file for lsmod */
    char *digest;           /* digest of .so file for lsmod */
    size_t argz_len;
    char *argz;
    int status;
    int errnum;
    bool muted;             /* module is under directive 42, no new messages */

    modpoller_cb_f poller_cb;
    void *poller_arg;
    module_status_cb_f status_cb;
    void *status_arg;

    struct disconnect *disconnect;

    zlist_t *rmmod;
    flux_msg_t *insmod;

    flux_t *h;               /* module's handle */

    zlist_t *subs;          /* subscription strings */
};

struct modhash {
    zhash_t *zh_byuuid;
    uint32_t rank;
    flux_t *broker_h;
    attr_t *attrs;
    char uuid_str[UUID_STR_LEN];
};

static int setup_module_profiling (module_t *p)
{
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.type", "module");
    cali_begin_int_byname ("flux.tid", syscall (SYS_gettid));
    cali_begin_int_byname ("flux.rank", p->modhash->rank);
    cali_begin_string_byname ("flux.name", p->name);
#endif
    return (0);
}

/*  Synchronize the FINALIZING state with the broker, so the broker
 *   can stop messages to this module until we're fully shutdown.
 */
static int module_finalizing (module_t *p)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (p->h,
                             "broker.module-status",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i}",
                             "status", FLUX_MODSTATE_FINALIZING))
        || flux_rpc_get (f, NULL)) {
        flux_log_error (p->h, "broker.module-status FINALIZING error");
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static void *module_thread (void *arg)
{
    module_t *p = arg;
    sigset_t signal_set;
    int errnum;
    char *uri = NULL;
    char **av = NULL;
    int ac;
    int mod_main_errno = 0;
    flux_msg_t *msg;
    flux_conf_t *conf;
    flux_future_t *f;

    setup_module_profiling (p);

    /* Connect to broker socket, enable logging, register built-in services
     */
    if (asprintf (&uri, "shmem://%s", p->uuid_str) < 0) {
        log_err ("asprintf");
        goto done;
    }
    if (!(p->h = flux_open (uri, 0))) {
        log_err ("flux_open %s", uri);
        goto done;
    }
    if (attr_cache_immutables (p->modhash->attrs, p->h) < 0) {
        log_err ("%s: error priming broker attribute cache", p->name);
        goto done;
    }
    flux_log_set_appname (p->h, p->name);
    /* Copy the broker's config object so that modules
     * can call flux_get_conf() and expect it to always succeed.
     */
    if (!(conf = flux_conf_copy (flux_get_conf (p->modhash->broker_h)))
            || flux_set_conf (p->h, conf) < 0) {
        flux_conf_decref (conf);
        log_err ("%s: error duplicating config object", p->name);
        goto done;
    }
    if (modservice_register (p->h, p) < 0) {
        log_err ("%s: modservice_register", p->name);
        goto done;
    }

    /* Block all signals
     */
    if (sigfillset (&signal_set) < 0) {
        log_err ("%s: sigfillset", p->name);
        goto done;
    }
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0) {
        log_errn (errnum, "pthread_sigmask");
        goto done;
    }

    /* Run the module's main().
     */
    ac = argz_count (p->argz, p->argz_len);
    if (!(av = calloc (1, sizeof (av[0]) * (ac + 1)))) {
        log_err ("calloc");
        goto done;
    }
    argz_extract (p->argz, p->argz_len, av);
    if (p->main (p->h, ac, av) < 0) {
        mod_main_errno = errno;
        if (mod_main_errno == 0)
            mod_main_errno = ECONNRESET;
        flux_log (p->h, LOG_CRIT, "module exiting abnormally");
    }

    /* Before processing unhandled requests, ensure that this module
     * is "muted" in the broker. This ensures the broker won't try to
     * feed a message to this module after we've closed the handle,
     * which could cause the broker to block.
     */
    if (module_finalizing (p) < 0)
        flux_log_error (p->h, "failed to set module state to finalizing");

    /* If any unhandled requests were received during shutdown,
     * respond to them now with ENOSYS.
     */
    while ((msg = flux_recv (p->h, FLUX_MATCH_REQUEST, FLUX_O_NONBLOCK))) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        flux_log (p->h, LOG_DEBUG, "responding to post-shutdown %s", topic);
        if (flux_respond_error (p->h, msg, ENOSYS, NULL) < 0)
            flux_log_error (p->h, "responding to post-shutdown %s", topic);
        flux_msg_destroy (msg);
    }
    if (!(f = flux_rpc_pack (p->h,
                             "broker.module-status",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i s:i}",
                             "status", FLUX_MODSTATE_EXITED,
                             "errnum", mod_main_errno))) {
        flux_log_error (p->h, "broker.module-status EXITED error");
        goto done;
    }
    flux_future_destroy (f);
done:
    free (uri);
    free (av);
    flux_close (p->h);
    p->h = NULL;
    return NULL;
}

const char *module_get_name (module_t *p)
{
    return p->name;
}

const char *module_get_uuid (module_t *p)
{
    return p->uuid_str;
}

static int module_get_idle (module_t *p)
{
    return flux_reactor_now (flux_get_reactor (p->modhash->broker_h)) - p->lastseen;
}

flux_msg_t *module_recvmsg (module_t *p)
{
    flux_msg_t *msg = NULL;
    int type;
    struct flux_msg_cred cred;

    if (!(msg = zmqutil_msg_recv (p->sock)))
        goto error;
    if (flux_msg_get_type (msg, &type) < 0)
        goto error;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (flux_msg_route_delete_last (msg) < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            if (flux_msg_route_push (msg, p->uuid_str) < 0)
                goto error;
            break;
        default:
            break;
    }
    /* All shmem:// connections to the broker have FLUX_ROLE_OWNER
     * and are "authenticated" as the instance owner.
     * Allow modules so endowed to change the userid/rolemask on messages when
     * sending on behalf of other users.  This is necessary for connectors
     * implemented as DSOs.
     */
    assert ((p->cred.rolemask & FLUX_ROLE_OWNER));
    if (flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (cred.userid == FLUX_USERID_UNKNOWN)
        cred.userid = p->cred.userid;
    if (cred.rolemask == FLUX_ROLE_NONE)
        cred.rolemask = p->cred.rolemask;
    if (flux_msg_set_cred (msg, cred) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int module_sendmsg (module_t *p, const flux_msg_t *msg)
{
    flux_msg_t *cpy = NULL;
    int type;
    const char *topic;
    int rc = -1;

    if (!msg)
        return 0;
    if (flux_msg_get_type (msg, &type) < 0
        || flux_msg_get_topic (msg, &topic) < 0)
        return -1;
    /* Muted modules only accept response to broker.module-status
     */
    if (p->muted) {
        if (type != FLUX_MSGTYPE_RESPONSE
            || strcmp (topic, "broker.module-status") != 0) {
            errno = ENOSYS;
            return -1;
        }
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST: { /* simulate DEALER socket */
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            if (flux_msg_route_push (cpy, p->modhash->uuid_str) < 0)
                goto done;
            if (zmqutil_msg_send (p->sock, cpy) < 0)
                goto done;
            break;
        }
        case FLUX_MSGTYPE_RESPONSE: { /* simulate ROUTER socket */
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            if (flux_msg_route_delete_last (cpy) < 0)
                goto done;
            if (zmqutil_msg_send (p->sock, cpy) < 0)
                goto done;
            break;
        }
        default:
            if (zmqutil_msg_send (p->sock, msg) < 0)
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
    const char *uuid;
    module_t *p;

    if (!msg)
        return 0;
    if (!(uuid = flux_msg_route_last (msg))) {
        errno = EPROTO;
        return -1;
    }
    if (!(p = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOSYS;
        return -1;
    }
    return module_sendmsg (p, msg);
}

int module_disconnect_arm (module_t *p,
                           const flux_msg_t *msg,
                           disconnect_send_f cb,
                           void *arg)
{
    if (!p->disconnect) {
        if (!(p->disconnect = disconnect_create (cb, arg)))
            return -1;
    }
    if (disconnect_arm (p->disconnect, msg) < 0)
        return -1;
    return 0;
}

static void module_destroy (module_t *p)
{
    int e;
    void *res;
    int saved_errno = errno;

    if (!p)
        return;

    if (p->t) {
        if ((e = pthread_join (p->t, &res)) != 0)
            log_errn_exit (e, "pthread_cancel");
    }

    /* Send disconnect messages to services used by this module.
     */
    disconnect_destroy (p->disconnect);

    flux_watcher_stop (p->broker_w);
    flux_watcher_destroy (p->broker_w);
    zsock_destroy (&p->sock);

#ifndef __SANITIZE_ADDRESS__
    dlclose (p->dso);
#endif
    free (p->digest);
    free (p->argz);
    free (p->name);
    if (p->rmmod) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (p->rmmod)))
            flux_msg_destroy (msg);
    }
    flux_msg_destroy (p->insmod);
    if (p->subs) {
        char *s;
        while ((s = zlist_pop (p->subs)))
            free (s);
        zlist_destroy (&p->subs);
    }
    zlist_destroy (&p->rmmod);
    free (p);
    errno = saved_errno;
}

/* Send shutdown request, broker to module.
 */
int module_stop (module_t *p)
{
    char *topic = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (asprintf (&topic, "%s.shutdown", p->name) < 0)
        goto done;
    if (!(f = flux_rpc (p->modhash->broker_h, topic, NULL,
                        FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE)))
        goto done;
    rc = 0;
done:
    free (topic);
    flux_future_destroy (f);
    return rc;
}

void module_mute (module_t *p)
{
    p->muted = true;
}

static void module_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    module_t *p = arg;
    p->lastseen = flux_reactor_now (r);
    if (p->poller_cb)
        p->poller_cb (p, p->poller_arg);
}

int module_start (module_t *p)
{
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

    if (p->argz) {
        free (p->argz);
        p->argz_len = 0;
    }
    if (argv && (e = argz_create (argv, &p->argz, &p->argz_len)) != 0)
        log_errn_exit (e, "argz_create");
}

void module_add_arg (module_t *p, const char *arg)
{
    int e;

    if ((e = argz_add (&p->argz, &p->argz_len, arg)) != 0)
        log_errn_exit (e, "argz_add");
}

void module_set_poller_cb (module_t *p, modpoller_cb_f cb, void *arg)
{
    p->poller_cb = cb;
    p->poller_arg = arg;
}

void module_set_status_cb (module_t *p, module_status_cb_f cb, void *arg)
{
    p->status_cb = cb;
    p->status_arg = arg;
}

void module_set_status (module_t *p, int new_status)
{
    assert (new_status != FLUX_MODSTATE_INIT);  /* illegal state transition */
    assert (p->status != FLUX_MODSTATE_EXITED); /* illegal state transition */
    int prev_status = p->status;
    p->status = new_status;
    if (p->status_cb)
        p->status_cb (p, prev_status, p->status_arg);
}

int module_get_status (module_t *p)
{
    return p->status;
}

void module_set_errnum (module_t *p, int errnum)
{
    p->errnum = errnum;
}

int module_get_errnum (module_t *p)
{
    return p->errnum;
}

int module_push_rmmod (module_t *p, const flux_msg_t *msg)
{
    flux_msg_t *cpy = flux_msg_copy (msg, false);
    if (!cpy)
        return -1;
    if (zlist_push (p->rmmod, cpy) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

flux_msg_t *module_pop_rmmod (module_t *p)
{
    return zlist_pop (p->rmmod);
}

/* There can be only one.
 */
int module_push_insmod (module_t *p, const flux_msg_t *msg)
{
    flux_msg_t *cpy = flux_msg_copy (msg, false);
    if (!cpy)
        return -1;
    if (p->insmod)
        flux_msg_destroy (p->insmod);
    p->insmod = cpy;
    return 0;
}

flux_msg_t *module_pop_insmod (module_t *p)
{
    flux_msg_t *msg = p->insmod;
    p->insmod = NULL;
    return msg;
}

module_t *module_add (modhash_t *mh, const char *path)
{
    module_t *p;
    void *dso;
    const char **mod_namep;
    mod_main_f *mod_main;
    size_t size;
    int rc;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_GLOBAL | FLUX_DEEPBIND))) {
        log_msg ("%s", dlerror ());
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
    if (!(p = calloc (1, sizeof (*p)))) {
        int saved_errno = errno;
        dlclose (dso);
        errno = saved_errno;
        return NULL;
    }
    p->main = mod_main;
    p->dso = dso;
    if (!(p->name = strdup (*mod_namep)))
        goto cleanup;
    if (!(p->digest = digest_file (path, &size)))
        goto cleanup;
    p->size = (int)size;
    uuid_generate (p->uuid);
    uuid_unparse (p->uuid, p->uuid_str);
    if (!(p->rmmod = zlist_new ()))
        goto nomem;
    if (!(p->subs = zlist_new ()))
        goto nomem;

    p->modhash = mh;

    /* Broker end of PAIR socket is opened here.
     */
    if (!(p->sock = zsock_new_pair (NULL))) {
        log_err ("zsock_new_pair");
        goto cleanup;
    }
    zsock_set_unbounded (p->sock);
    zsock_set_linger (p->sock, 5);
    if (zsock_bind (p->sock, "inproc://%s", module_get_uuid (p)) < 0) {
        log_err ("zsock_bind inproc://%s", module_get_uuid (p));
        goto cleanup;
    }
    if (!(p->broker_w = zmqutil_watcher_create (
                                        flux_get_reactor (p->modhash->broker_h),
                                        p->sock,
                                        FLUX_POLLIN,
                                        module_cb,
                                        p))) {
        log_err ("zmqutil_watcher_create");
        goto cleanup;
    }
    /* Set creds for connection.
     * Since this is a point to point connection between broker threads,
     * credentials are always those of the instance owner.
     */
    p->cred.userid = getuid ();
    p->cred.rolemask = FLUX_ROLE_OWNER;

    /* Update the modhash.
     */
    rc = zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    assert (rc == 0); /* uuids are by definition unique */
    zhash_freefn (mh->zh_byuuid, module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
    return p;
nomem:
    errno = ENOMEM;
cleanup:
    module_destroy (p);
    return NULL;
}

void module_remove (modhash_t *mh, module_t *p)
{
    zhash_delete (mh->zh_byuuid, module_get_uuid (p));
}

modhash_t *modhash_create (void)
{
    modhash_t *mh = calloc (1, sizeof (*mh));
    if (!mh)
        return NULL;
    if (!(mh->zh_byuuid = zhash_new ())) {
        errno = ENOMEM;
        modhash_destroy (mh);
        return NULL;
    }
    return mh;
}

void modhash_destroy (modhash_t *mh)
{
    int saved_errno = errno;
    const char *uuid;
    module_t *p;
    int e;

    if (mh) {
        if (mh->zh_byuuid) {
            FOREACH_ZHASH (mh->zh_byuuid, uuid, p) {
                if (p->t) {
                    if ((e = pthread_cancel (p->t)) != 0 && e != ESRCH)
                        log_errn (e, "pthread_cancel");
                }
            }
            zhash_destroy (&mh->zh_byuuid);
        }
        free (mh);
    }
    errno = saved_errno;
}

void modhash_initialize (modhash_t *mh,
                         flux_t *h,
                         const char *uuid,
                         attr_t *attrs)
{
    mh->broker_h = h;
    mh->attrs = attrs;
    flux_get_rank (h, &mh->rank);
    strncpy (mh->uuid_str, uuid, sizeof (mh->uuid_str) - 1);
}

json_t *module_get_modlist (modhash_t *mh, struct service_switch *sw)
{
    json_t *mods = NULL;
    zlist_t *uuids = NULL;
    char *uuid;
    module_t *p;

    if (!(mods = json_array()))
        goto nomem;
    if (!(uuids = zhash_keys (mh->zh_byuuid)))
        goto nomem;
    uuid = zlist_first (uuids);
    while (uuid) {
        if ((p = zhash_lookup (mh->zh_byuuid, uuid))) {
            json_t *svcs;
            json_t *entry;

            if (!(svcs  = service_list_byuuid (sw, uuid)))
                goto nomem;
            if (!(entry = json_pack ("{s:s s:i s:s s:i s:i s:o}",
                                     "name", module_get_name (p),
                                     "size", p->size,
                                     "digest", p->digest,
                                      "idle", module_get_idle (p),
                                      "status", p->status,
                                      "services", svcs))) {
                json_decref (svcs);
                goto nomem;
            }
            if (json_array_append_new (mods, entry) < 0) {
                json_decref (entry);
                goto nomem;
            }
        }
        uuid = zlist_next (uuids);
    }
    zlist_destroy (&uuids);
    return mods;
nomem:
    zlist_destroy (&uuids);
    json_decref (mods);
    errno = ENOMEM;
    return NULL;
}

module_t *module_lookup (modhash_t *mh, const char *uuid)
{
    module_t *m;

    if (!(m = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOENT;
        return NULL;
    }
    return m;
}

module_t *module_lookup_byname (modhash_t *mh, const char *name)
{
    zlist_t *uuids;
    char *uuid;
    module_t *result = NULL;

    if (!(uuids = zhash_keys (mh->zh_byuuid))) {
        errno = ENOMEM;
        return NULL;
    }
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
    char *cpy = NULL;
    int rc = -1;

    if (!p) {
        errno = ENOENT;
        goto done;
    }
    if (!(cpy = strdup (topic)))
        goto done;
    if (zlist_push (p->subs, cpy) < 0) {
        free (cpy);
        errno = ENOMEM;
        goto done;
    }
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
            free (s);
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

module_t *module_first (modhash_t *mh)
{
    return zhash_first (mh->zh_byuuid);
}

module_t *module_next (modhash_t *mh)
{
    return zhash_next (mh->zh_byuuid);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
