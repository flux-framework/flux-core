/* eventsrv.c - event relay */

/* N.B. For a given epgm uri, there can be only one publisher and one
 * subscriber per node.  Messages published on the the same node will not
 * be "looped back" to a subscriber on the same node via epgm.
 * This epgm behavior is an invariant presumed in the design of this module.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "plugin.h"
#include "util.h"
#include "log.h"
#include "security.h"

typedef struct {
    flux_t h;
    char *local_inproc_uri;
    char *local_ipc_uri;
    char *local_tcp_uri;
    void *local_zs_pub;
    char *mcast_uri;
    void *mcast_zs_pub;
    void *mcast_zs_sub;
    zctx_t *zctx;
    flux_sec_t sec;
    bool mcast_all_publish;
    char *tcp_if;
    bool treeroot;
    pid_t pid;
    char hostname[HOST_NAME_MAX + 1];
} ctx_t;

static void freectx (ctx_t *ctx)
{
    if (ctx->tcp_if)
        free (ctx->tcp_if);
    if (ctx->local_inproc_uri)
        free (ctx->local_inproc_uri);
    if (ctx->local_ipc_uri)
        free (ctx->local_ipc_uri);
    if (ctx->local_tcp_uri)
        free (ctx->local_tcp_uri);
    if (ctx->mcast_uri)
        free (ctx->mcast_uri);
    if (ctx->local_zs_pub)
        zsocket_destroy (ctx->zctx, ctx->local_zs_pub);
    if (ctx->mcast_zs_pub)
        zsocket_destroy (ctx->zctx, ctx->mcast_zs_pub);
    if (ctx->mcast_zs_sub)
        zsocket_destroy (ctx->zctx, ctx->mcast_zs_sub);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "eventsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->zctx = flux_get_zctx (h);
        ctx->sec = flux_get_sec (h);
        ctx->treeroot = flux_treeroot (h);
        ctx->pid = getpid ();
        if (gethostname (ctx->hostname, HOST_NAME_MAX) < 0)
            err_exit ("gethostname");
        flux_aux_set (h, "eventsrv", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

static int mcast_event_cb (flux_t h, void *zs, short revents, void *arg)
{
    ctx_t *ctx = arg;
    zmsg_t *zmsg;

    if ((zmsg = zmsg_recv (zs))) {
        if (zmsg_content_size (zmsg) == 0)
            goto done; /* we see this on startup - don't log (zmq epgm bug?) */
        if (flux_sec_unmunge_zmsg (ctx->sec, &zmsg) < 0) {
            flux_log (h, LOG_INFO, "%s: unmunge: %s", __FUNCTION__,
                      flux_sec_errstr (ctx->sec));
            goto done;
        }
        if (zmsg_send (&zmsg, ctx->local_zs_pub) < 0) {
            flux_log (h, LOG_ERR, "%s: zmsg_send: %s", __FUNCTION__,
                      strerror (errno));
            goto done;
        }
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return 0;
}

static int pub_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    (void)flux_response_sendmsg (h, zmsg); /* forward downstream */
    return 0;
}

static int pub_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    const char *topic;
    json_object *payload;
    zmsg_t *event = NULL, *cpy = NULL;

    if (!ctx->treeroot && !ctx->mcast_zs_pub) {
        (void)flux_request_sendmsg (h, zmsg); /* forward upstream */
        goto done;
    }
    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || !request) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_string (request, "topic", &topic) < 0
            || !(payload = json_object_object_get (request, "payload"))) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    if (!(event = cmb_msg_encode ((char *)topic, payload))) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    /* Publish event to epgm (if set up)
     */
    if (ctx->mcast_zs_pub) {
        if (!(cpy = zmsg_dup (event)))
            oom ();
        if (flux_sec_munge_zmsg (ctx->sec, &cpy) < 0) {
            flux_respond_errnum (h, zmsg, errno);
            flux_log (h, LOG_ERR, "%s: discarding message: %s",
                      __FUNCTION__, flux_sec_errstr (ctx->sec));
            goto done;
        }
        if (zmsg_send (&cpy, ctx->mcast_zs_pub) < 0) {
            flux_respond_errnum (h, zmsg, errno ? errno : EIO);
            goto done;
        }
    }
    /* Publish event locally.
     */
    if (zmsg_send (&event, ctx->local_zs_pub) < 0) {
        flux_respond_errnum (h, zmsg, errno ? errno : EIO);
        goto done;
    }
    flux_respond_errnum (h, zmsg, 0);
done:        
    if (request)
        json_object_put (request); 
    if (event)
        zmsg_destroy (&event);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int geturi_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    json_object *response = util_json_object_new_object ();
    const char *hostname;
    int pid;
    char *uri;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || !request) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_string (request, "hostname", &hostname) < 0
            || util_json_object_get_int (request, "pid", &pid) < 0) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    if (strcmp (hostname, ctx->hostname)) {  /* different host, use tcp:// */
        if (!ctx->local_tcp_uri) {
            if (zsocket_bind (ctx->local_zs_pub, "tcp://%s:*",
                              ctx->tcp_if ? ctx->tcp_if : "eth0") < 0) {
                flux_log (h, LOG_ERR, "zsocket_bind tcp://*:*: %s",
                          strerror (errno));
                flux_respond_errnum (h, zmsg, errno);
                goto done;
            }
            ctx->local_tcp_uri = zsocket_last_endpoint (ctx->local_zs_pub);
        }
        uri = ctx->local_tcp_uri;
    } else if (pid != ctx->pid) {            /* different process use ipc:// */
        if (!ctx->local_ipc_uri) {
            if (zsocket_bind (ctx->local_zs_pub, "ipc://*") < 0) {
                flux_log (h, LOG_ERR, "zsocket_bind ipc://* %s",
                          strerror (errno));
                flux_respond_errnum (h, zmsg, errno);
                goto done;
            }
            ctx->local_ipc_uri = zsocket_last_endpoint (ctx->local_zs_pub);
        }
        uri = ctx->local_ipc_uri;
    } else {                                 /* same process use inproc:// */
        uri = ctx->local_inproc_uri;
    }
    util_json_object_add_string (response, "uri", uri);
    flux_respond (h, zmsg, response);
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "event.pub",   pub_request_cb },
    { FLUX_MSGTYPE_RESPONSE, "event.pub",   pub_response_cb },
    { FLUX_MSGTYPE_REQUEST,  "event.geturi",geturi_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    int rc = -1;
    zuuid_t *uuid;

    /* Read global configuration once.
     *   (Handling live change is courting deadlock)
     */
    (void)kvs_get_string (h, "conf.event.tcp-if", &ctx->tcp_if);
    (void)kvs_get_string (h, "conf.event.mcast-uri", &ctx->mcast_uri);
    (void)kvs_get_boolean (h, "conf.event.mcast-all-publish",
                           &ctx->mcast_all_publish);


    /* Create a local PUB socket relay with an inproc:// endpoint
     *   Other endpoints will be added as needed by event.geturi requests.
     */
    if (!(uuid = zuuid_new ()))
        oom ();
    if (asprintf (&ctx->local_inproc_uri, "inproc://%s", zuuid_str (uuid)) < 0)
        oom ();
    zuuid_destroy (&uuid);
    if (!(ctx->local_zs_pub = zsocket_new (ctx->zctx, ZMQ_PUB))) {
        flux_log (h, LOG_ERR, "zsocket_new: %s", strerror (errno));
        goto done;
    }
    zsocket_set_sndhwm (ctx->local_zs_pub, 0);
    if (flux_sec_ssockinit (ctx->sec, ctx->local_zs_pub) < 0) {
        flux_log (h, LOG_ERR, "flux_sec_ssockinit: %s",
                  flux_sec_errstr (ctx->sec));
        goto done;
    }
    if (zsocket_bind (ctx->local_zs_pub, "%s", ctx->local_inproc_uri) < 0) {
        flux_log (h, LOG_ERR, "zsocket_bind %s: %s", ctx->local_inproc_uri,
                  strerror (errno));
        goto done;
    }

    /* conf.event.mcast-uri - if set, relay events from epgm to ipc socket
     */
    if (ctx->mcast_uri) {
        if (!(ctx->mcast_zs_sub = zsocket_new (ctx->zctx, ZMQ_SUB))) {
            flux_log (ctx->h, LOG_ERR, "zsocket_new: %s", strerror (errno));
            goto done;
        }
        zsocket_set_rcvhwm (ctx->mcast_zs_sub, 0);
        if (zsocket_connect (ctx->mcast_zs_sub, "%s", ctx->mcast_uri) < 0) {
            flux_log (ctx->h, LOG_ERR, "zsocket_connect%s: %s", ctx->mcast_uri,
                      strerror (errno));
            goto done;
        }
        zsocket_set_subscribe (ctx->mcast_zs_sub, "");
        if (flux_zshandler_add (ctx->h, ctx->mcast_zs_sub, ZMQ_POLLIN,
                                mcast_event_cb, ctx) < 0) {
            flux_log (ctx->h, LOG_ERR, "flux_zshandler_add: %s",
                      strerror (errno));
            goto done;
        }
    }
    /* conf.event.mcast-all-publish - all nodes can publish to mcast-uri
     *   Without this, only the root will publish to mcast-uri'
     *   and the rest will fwd event.pub requests upstream.
     */
    if (ctx->mcast_uri && (ctx->treeroot || ctx->mcast_all_publish)) {
        if (!(ctx->mcast_zs_pub = zsocket_new (ctx->zctx, ZMQ_PUB))) {
            flux_log (ctx->h, LOG_ERR, "zsocket_new: %s", strerror (errno));
            goto done;
        }
        zsocket_set_sndhwm (ctx->mcast_zs_pub, 0);
        if (zsocket_connect (ctx->mcast_zs_pub, "%s", ctx->mcast_uri) < 0) {
            flux_log (ctx->h, LOG_ERR, "zsocket_connect %s: %s",
                      ctx->mcast_uri, strerror (errno));
            goto done;
        }
    }

    /* Start reactor.
     */
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        goto done;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        goto done;
    }
    rc = 0;
done:
    return rc;
}

MOD_NAME ("event");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
