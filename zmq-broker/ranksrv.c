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
    void *right_zs;
    zctx_t *zctx;
    int rank;
    flux_sec_t sec;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    if (ctx->right_uri)
        free (ctx->right_uri);
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

static int sendreq_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "rank.sendreq",    sendreq_request_cb},
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int ranksrv_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    int rc = -1;

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
    .main = ranksrv_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
