/* ranksrv.c - relay requests to specific cmb ranks on ring overlay */

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
#include "flux.h"
#include "security.h"
#include "shortjson.h"

typedef struct {
    flux_t h;
    char *right_uri;
    char *right_id;
    void *right_zs;
    zctx_t *zctx;
    int rank;
    flux_sec_t sec;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    if (ctx->right_uri)
        free (ctx->right_uri);
    if (ctx->right_id)
        free (ctx->right_id);
    if (ctx->right_zs)
        zsocket_destroy (ctx->zctx, ctx->right_zs);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "ranksrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->zctx = flux_get_zctx (h);
        ctx->sec = flux_get_sec (h);
        ctx->rank = flux_rank (h);
        flux_aux_set (h, "ranksrv", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

static void unwrap (zmsg_t **zmsg, const char **tp, JSON *pp)
{
    zframe_t *zf[2];
    const char *s;
    int i;

    for (i = 0; i < 2; i++) { 
        zf[i] = zmsg_last (*zmsg);
        assert (zf[i] != NULL);
        zmsg_remove (*zmsg, zf[i]);
    }
    if (zmsg_addstr (*zmsg, *tp) < 0)
        oom ();
    s = Jtostr (*pp);
    if (s && zmsg_addstr (*zmsg, s) < 0)
        oom ();
    for (i = 0; i < 2; i++) /* destroys *tp and *pp */
        zframe_destroy (&zf[i]);
    *pp = NULL;
    *tp = NULL;
}

static bool looped (ctx_t *ctx, zmsg_t *zmsg)
{
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) > 0) {
        if (zframe_streq (zf, ctx->right_id))
            return true;
        zf = zmsg_next (zmsg);
    }
    return false;        
}

static int fwd_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL, payload = NULL;
    int rank;
    const char *topic;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || !request) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!Jget_int (request, "rank", &rank)
                || !Jget_str (request, "topic", &topic)
                || !Jget_obj (request, "payload", &payload)) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    if (rank == ctx->rank) {
        unwrap (zmsg, &topic, &payload);
        if (flux_request_sendmsg (ctx->h, zmsg) < 0) {
            flux_log (h, LOG_ERR, "%s: flux_request_sendmsg: %s",
                      __FUNCTION__, strerror (errno));
            goto done;
        }
    } else if (looped (ctx, *zmsg) || !ctx->right_zs) {
        unwrap (zmsg, &topic, &payload);
        if (flux_respond_errnum (h, zmsg, EHOSTUNREACH) < 0) {
            flux_log (h, LOG_ERR, "%s: flux_respond_errnum: %s",
                      __FUNCTION__, strerror (errno));
            goto done;
        }
    } else {
        if (zmsg_send (zmsg, ctx->right_zs) < 0) {
            flux_log (h, LOG_ERR, "%s: %s", __FUNCTION__, strerror (errno));
            goto done;
        }
    }
done:
    Jput (request);
    Jput (payload);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    if (flux_response_sendmsg (h, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_resopnse_sendmsg: %s",
                  __FUNCTION__, strerror (errno));
        goto done;
    }
done:
    return 0;
}

int ring_response_cb (flux_t h, void *zs, short revents, void *arg)
{
    zmsg_t *zmsg = zmsg_recv (zs);

    if (zmsg) {
        if (flux_response_sendmsg (h, &zmsg) < 0) {
            flux_log (h, LOG_ERR, "%s: flux_resopnse_sendmsg: %s",
                      __FUNCTION__, strerror (errno));
            goto done;
        }
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return 0; 
}

static void *init_dealer (ctx_t *ctx, const char *id, const char *uri)
{
    void *s;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER))) {
        flux_log (ctx->h, LOG_ERR, "zsocket_new: %s", strerror (errno));
        goto error;
    }
    zsocket_set_sndhwm (s, 0);
    zsocket_set_identity (s, (char *)id);
    if (flux_sec_csockinit (ctx->sec, s) < 0) {
        flux_log (ctx->h, LOG_ERR, "flux_sec_csockinit: %s",
                  flux_sec_errstr (ctx->sec));
        goto error;
    }
    if (zsocket_connect (s, "%s", uri) < 0) {
        flux_log (ctx->h, LOG_ERR, "zsocket_connect %s: %s", uri,
                  strerror (errno));
        goto error;
    }
    if (flux_zshandler_add (ctx->h, s, ZMQ_POLLIN, ring_response_cb, ctx) < 0) {
        flux_log (ctx->h, LOG_ERR, "flux_zshandler_add: %s", strerror (errno));
        goto error;
    }
    return s;
error:
    if (s)
        zsocket_destroy (ctx->zctx, s);
    return NULL;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "rank.fwd",        fwd_request_cb},
    { FLUX_MSGTYPE_RESPONSE, "*",               response_cb},
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    int rc = -1;

    if (!args || !(ctx->right_uri = zhash_lookup (args, "right-uri"))) {
        flux_log (h, LOG_ERR, "no sockets configured");
        goto done;
    }
    if (asprintf (&ctx->right_id, "%dr", ctx->rank) < 0)
        oom ();
    if (!(ctx->right_zs = init_dealer (ctx, ctx->right_id, ctx->right_uri)))
        goto done;

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
