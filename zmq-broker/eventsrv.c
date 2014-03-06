/* eventsrv.c - event relay */

/* Provides a local event nexus on an ipc:// socket.
 * Publish events on request.  Provide ipc uri on request.
 * Establish a relay between epgm:// socket and ipc:// socket.
 */

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
    char *local_uri;
    void *local_zs_pub;
    char *mcast_uri;
    void *mcast_zs_pub;
    void *mcast_zs_sub;
    zctx_t *zctx;
    flux_sec_t sec;
    bool mcast_all_publish;
    bool treeroot;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    if (ctx->local_uri)
        free (ctx->local_uri);
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

static void reconfig (ctx_t *ctx)
{
    if (ctx->mcast_zs_pub) {
        zsocket_destroy (ctx->zctx, ctx->mcast_zs_pub);
        ctx->mcast_zs_pub = NULL;
    }
    if (ctx->mcast_zs_sub) {
        zsocket_destroy (ctx->zctx, ctx->mcast_zs_sub);
        ctx->mcast_zs_sub = NULL;
    }
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
done:
    return;
}

static void set_config (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    char *key;
    char *mcast_uri = NULL;
    bool mcast_all_publish = false;;
    bool config_changed = false;

    if (errnum == 0) {
        key = kvsdir_key_at (dir, "mcast-uri");
        (void)kvs_get_string (ctx->h, key, &mcast_uri);
        free (key);

        key = kvsdir_key_at (dir, "mcast-all-publish");
        (void)kvs_get_boolean (ctx->h, key, &mcast_all_publish);
        free (key);
    }
    if (mcast_uri) {
        if (!ctx->mcast_uri || strcmp (mcast_uri, ctx->mcast_uri) != 0) {
            if (ctx->mcast_uri)
                free (ctx->mcast_uri);
            ctx->mcast_uri = xstrdup (mcast_uri);
            config_changed = true;
        }
        free (mcast_uri);
    } else if (ctx->mcast_uri) {
        free (ctx->mcast_uri);
        ctx->mcast_uri = NULL;
        config_changed = true;
    }
    if (ctx->mcast_all_publish != mcast_all_publish) {
        ctx->mcast_all_publish = mcast_all_publish;
        config_changed = true;
    }
    if (config_changed)
        reconfig (ctx);
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "event.pub",   pub_request_cb },
    { FLUX_MSGTYPE_RESPONSE, "event.pub",   pub_response_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int eventsrv_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    int rc = -1;
    char *s;
    uid_t uid = geteuid ();

    /* event:local-uri - override default ipc socket
     *   Publish/relay events here.
     */
    if ((s = zhash_lookup (args, "event:local-uri")))
        ctx->local_uri = xstrdup (s);
    else if (asprintf (&ctx->local_uri, "ipc:///tmp/flux_event_uid%d", uid) < 0)
        oom ();
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
    if (zsocket_bind (ctx->local_zs_pub, ctx->local_uri) < 0) {
        flux_log (h, LOG_ERR, "zsocket_bind %s: %s", ctx->local_uri,
                  strerror (errno));
        goto done;
    }

    /* Fetch global config from kvs
     *   config.event.mcast-uri: "epgm://..."
     *   config.event.mcast-all-publish: true|false
     */
    if (kvs_watch_dir (h, set_config, ctx, "conf.event") < 0) {
        err ("log: %s", "conf.log");
        return -1;
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

const struct plugin_ops ops = {
    .main = eventsrv_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
