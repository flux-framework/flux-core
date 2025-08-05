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
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <signal.h>
#include <pthread.h>
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libflux/plugin_private.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/basename.h"
#include "src/common/librouter/subhash.h"
#include "ccan/str/str.h"

#include "module.h"
#include "modservice.h"

struct broker_module {
    flux_t *h;              /* ref to broker's internal flux_t handle */

    flux_watcher_t *broker_w;

    double lastseen;

    flux_t *h_broker_end;   /* broker end of interthread channel */

    struct module_args args;/* passed to module thread via (void *) param */

    uuid_t uuid;            /* uuid for unique request sender identity */
    char uuid_str[UUID_STR_LEN];
    pthread_t t;            /* module thread */
    char *name;
    int status;
    int errnum;
    bool muted;             /* module is under directive 42, no new messages */
    struct aux_item *aux;

    modpoller_cb_f poller_cb;
    void *poller_arg;
    module_status_cb_f status_cb;
    void *status_arg;

    struct disconnect *disconnect;
    struct flux_msglist *deferred_messages;
    struct subhash *sub;
};

void *module_thread (void *arg); // defined in module_thread.c

static int attr_cache_to_json (flux_t *h, json_t **cachep)
{
    json_t *cache;
    const char *name;

    if (!(cache = json_object ()))
        return -1;
    name = flux_attr_cache_first (h);
    while (name) {
        json_t *val = json_string (flux_attr_get (h, name));
        if (!val || json_object_set_new (cache, name, val) < 0) {
            json_decref (val);
            goto error;
        }
        name = flux_attr_cache_next (h);
    }
    *cachep = cache;
    return 0;
error:
    json_decref (cache);
    return -1;
}

static void module_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    module_t *p = arg;
    p->lastseen = flux_reactor_now (r);
    if (p->poller_cb)
        p->poller_cb (p, p->poller_arg);
}

/* Create a welcome message for the new module thread containing
 * - module uuid
 * - arguments to mod_main()
 * - cacheable broker attributes and their values
 * - current config object
 * This is called from the broker thread.
 */
static flux_msg_t *welcome_encode (flux_t *h, json_t *args)
{
    flux_msg_t *msg = NULL;
    json_t *attrs;
    json_t *conf;

    /* Optimization: create attribute cache to be primed in the module's
     * flux_t handle.  Priming the cache avoids a synchronous RPC from
     * flux_attr_get(3) for common attrs like rank, etc.
     */
    if (attr_cache_to_json (h, &attrs) < 0) {
        errno = ENOMEM;
        return NULL;
    }
    if (flux_conf_unpack (flux_get_conf (h), NULL, "o", &conf) < 0)
        goto error;
    if (!(msg = flux_request_encode ("welcome", NULL))
        || flux_msg_pack (msg,
                          "{s:O? s:O s:O}",
                          "args", args,
                          "attrs", attrs,
                          "conf", conf) < 0)
        goto error;
    json_decref (attrs);
    return msg;
error:
    flux_msg_decref (msg);
    ERRNO_SAFE_WRAP (json_decref, attrs);
    return NULL;
}

module_t *module_create (flux_t *h,
                         const char *parent_uuid,
                         const char *name,
                         mod_main_f mod_main,
                         json_t *mod_args,
                         flux_error_t *error)
{
    flux_reactor_t *r = flux_get_reactor (h);
    module_t *p;

    if (!(p = calloc (1, sizeof (*p))))
        goto nomem;
    p->h = h;
    if (!(p->name = strdup (name)))
        goto nomem;
    uuid_generate (p->uuid);
    uuid_unparse (p->uuid, p->uuid_str);

    if (!(p->sub = subhash_create ())) {
        errprintf (error, "error creating subscription hash");
        goto cleanup;
    }

    /* Broker end of interthread pair is opened here.
     */
    // copying 13 + 37 + 1 = 51 bytes into 128 byte buffer cannot fail
    char uri[128];
    (void)snprintf (uri, sizeof (uri), "interthread://%s", p->uuid_str);
    if (!(p->h_broker_end = flux_open (uri, FLUX_O_NOREQUEUE))
        || flux_opt_set (p->h_broker_end,
                         FLUX_OPT_ROUTER_NAME,
                         parent_uuid,
                         strlen (parent_uuid) + 1) < 0
        || flux_set_reactor (p->h_broker_end, r) < 0) {
        errprintf (error, "could not create broker end of %s", uri);
        goto cleanup;
    }
    if (!(p->broker_w = flux_handle_watcher_create (r,
                                                    p->h_broker_end,
                                                    FLUX_POLLIN,
                                                    module_cb,
                                                    p))) {
        errprintf (error, "could not create %s flux handle watcher", p->name);
        goto cleanup;
    }
    flux_msg_t *msg;
    if (!(msg = welcome_encode (h, mod_args))
        || flux_send_new (p->h_broker_end, &msg, 0) < 0) {
        errprintf (error, "error sending %s welcome message", p->name);
        flux_msg_decref (msg);
        goto cleanup;
    }
    /* Prepare (void *) argument to module thread.
     * Take care not to change these while the thread is executing.
     */
    p->args.uuid = p->uuid_str;
    p->args.name = p->name;
    p->args.main = mod_main;
    return p;
nomem:
    errprintf (error, "out of memory");
    errno = ENOMEM;
cleanup:
    module_destroy (p);
    return NULL;
}

const char *module_get_name (module_t *p)
{
    return p && p->name ? p->name : "unknown";
}

const char *module_get_uuid (module_t *p)
{
    return p ? p->uuid_str : "unknown";
}

double module_get_lastseen (module_t *p)
{
    return p ? p->lastseen : 0;
}

int module_get_status (module_t *p)
{
    return p ? p->status : 0;
}

void *module_aux_get (module_t *p, const char *name)
{
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (p->aux, name);
}

int module_aux_set (module_t *p,
                    const char *name,
                    void *val,
                    flux_free_f destroy)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&p->aux, name, val, destroy);
}

flux_msg_t *module_recvmsg (module_t *p)
{
    flux_msg_t *msg;
    msg = flux_recv (p->h_broker_end, FLUX_MATCH_ANY, FLUX_O_NONBLOCK);
    return msg;
}

int module_sendmsg_new (module_t *p, flux_msg_t **msg)
{
    int type;
    const char *topic;

    if (!msg || !*msg)
        return 0;
    if (flux_msg_get_type (*msg, &type) < 0
        || flux_msg_get_topic (*msg, &topic) < 0)
        return -1;
    /* Muted modules only accept response to module.status
     */
    if (p->muted) {
        if (type != FLUX_MSGTYPE_RESPONSE
            || !streq (topic, "module.status")) {
            errno = ENOSYS;
            return -1;
        }
    }
    if (p->deferred_messages) {
        if (flux_msglist_append (p->deferred_messages, *msg) < 0)
            return -1;
        flux_msg_decref (*msg);
        *msg = NULL;
        return 0;
    }
    return flux_send_new (p->h_broker_end, msg, 0);
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

void module_destroy (module_t *p)
{
    int e;
    void *res;
    int saved_errno = errno;

    if (!p)
        return;

    if (p->t) {
        if ((e = pthread_join (p->t, &res)) != 0)
            log_errn_exit (e, "pthread_join");
        if (p->status != FLUX_MODSTATE_EXITED) {
            /* Calls broker.c module_status_cb() => service_remove_byuuid()
             * and releases a reference on 'p'.  Without this, disconnect
             * requests sent when other modules are destroyed can still find
             * this service name and trigger a use-after-free segfault.
             * See also: flux-framework/flux-core#4564.
             */
            module_set_status (p, FLUX_MODSTATE_EXITED);
        }
        if (res == PTHREAD_CANCELED)
            flux_log (p->h, LOG_DEBUG, "%s thread was canceled", p->name);
    }

    /* Send disconnect messages to services used by this module.
     */
    disconnect_destroy (p->disconnect);

    flux_watcher_stop (p->broker_w);
    flux_watcher_destroy (p->broker_w);
    flux_close (p->h_broker_end);

    free (p->name);
    flux_msglist_destroy (p->deferred_messages);
    subhash_destroy (p->sub);
    aux_destroy (&p->aux);
    free (p);
    errno = saved_errno;
}

/* Send shutdown request, broker to module.
 */
int module_stop (module_t *p, flux_t *h)
{
    char *topic = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (asprintf (&topic, "%s.shutdown", p->name) < 0)
        goto done;
    if (!(f = flux_rpc (h,
                        topic,
                        NULL,
                        FLUX_NODEID_ANY,
                        FLUX_RPC_NORESPONSE)))
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

int module_set_defer (module_t *p, bool flag)
{
    if (flag && !p->deferred_messages) {
        if (!(p->deferred_messages = flux_msglist_create ()))
            return -1;
    }
    if (!flag && p->deferred_messages) {
        const flux_msg_t *msg;
        while ((msg = flux_msglist_pop (p->deferred_messages))) {
            if (flux_send_new (p->h_broker_end, (flux_msg_t **)&msg, 0) < 0) {
                flux_msg_decref (msg);
                return -1;
            }
        }
        flux_msglist_destroy (p->deferred_messages);
        p->deferred_messages = NULL;
    }
    return 0;
}

int module_start (module_t *p)
{
    int errnum;
    int rc = -1;

    flux_watcher_start (p->broker_w);
    if ((errnum = pthread_create (&p->t, NULL, module_thread, &p->args))) {
        errno = errnum;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int module_cancel (module_t *p, flux_error_t *error)
{
    if (p->t) {
        int e;
        if ((e = pthread_cancel (p->t)) != 0 && e != ESRCH) {
            errprintf (error, "pthread_cancel: %s", strerror (e));
            return -1;
        }
    }
    return 0;
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
    if (new_status == FLUX_MODSTATE_INIT || p->status == FLUX_MODSTATE_EXITED)
        return; // illegal state transitions
    int prev_status = p->status;
    p->status = new_status;
    if (p->status_cb)
        p->status_cb (p, prev_status, p->status_arg);
}

void module_set_errnum (module_t *p, int errnum)
{
    p->errnum = errnum;
}

int module_get_errnum (module_t *p)
{
    return p->errnum;
}

int module_subscribe (module_t *p, const char *topic)
{
    return subhash_subscribe (p->sub, topic);
}

int module_unsubscribe (module_t *p, const char *topic)
{
    return subhash_unsubscribe (p->sub, topic);
}

bool module_is_subscribed (module_t *p, const char *topic)
{
    return subhash_topic_match (p->sub, topic);
}

ssize_t module_get_send_queue_count (module_t *p)
{
    size_t count;
    if (flux_opt_get (p->h_broker_end,
                      FLUX_OPT_SEND_QUEUE_COUNT,
                      &count,
                      sizeof (count)) < 0)
        return -1;
    return count;
}

ssize_t module_get_recv_queue_count (module_t *p)
{
    size_t count;
    if (flux_opt_get (p->h_broker_end,
                      FLUX_OPT_RECV_QUEUE_COUNT,
                      &count,
                      sizeof (count)) < 0)
        return -1;
    return count;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
