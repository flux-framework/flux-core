#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    flux_t h;
    zhash_t *ping_requests;
    int ping_seq;
    zlist_t *clog_requests;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    zlist_t *keys = zhash_keys (ctx->ping_requests);
    char *key = zlist_first (keys);
    while (key) {
        zmsg_t *zmsg = zhash_lookup (ctx->ping_requests, key);
        zmsg_destroy (&zmsg);
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);
    zhash_destroy (&ctx->ping_requests);

    zmsg_t *zmsg;
    while ((zmsg = zlist_pop (ctx->clog_requests)))
        zmsg_destroy (&zmsg);
    zlist_destroy (&ctx->clog_requests);

    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "req");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (!(ctx->ping_requests = zhash_new ()))
            oom ();
        if (!(ctx->clog_requests = zlist_new ()))
            oom ();
        flux_aux_set (h, "req", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

/* Return number of queued clog requests
 */
static int count_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = getctx (h);
    JSON o = Jnew ();

    Jadd_int (o, "count", zlist_size (ctx->clog_requests));
    if (flux_json_respond (h, o, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                  strerror (errno));
    Jput (o);
    return 0;
}

/* Don't reply to request - just queue it for later.
 */
static int clog_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = getctx (h);

    if (zlist_push (ctx->clog_requests, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    return 0;
}

/* Reply to all queued requests.
 */
static int flush_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = getctx (h);
    zmsg_t *z;

    while ((z = zlist_pop (ctx->clog_requests))) {
        /* send clog response */
        if (flux_err_respond (h, 0, &z) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                          strerror (errno));
    }
    /* send flush response */
    if (flux_err_respond (h, 0, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
    return 0;
}

/* Accept a json payload, verify it and return error if it doesn't
 * match expected.
 */
static int sink_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = NULL;
    double d;

    if (flux_json_request_decode (*zmsg, &o) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    if (!Jget_double (o, "pi", &d) || d != 3.14) {
        if (flux_err_respond (h, EPROTO, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    if (flux_err_respond (h, 0, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
done:
    Jput (o);
    return 0;
}

/* Return a fixed json payload
 */
static int src_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = Jnew ();

    Jadd_int (o, "wormz", 42);
    if (flux_json_respond (h, o, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                  strerror (errno));
    Jput (o);
    return 0;
}

/* Return 'n' sequenced responses.
 */
static int nsrc_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = Jnew ();
    int i, count;

    if (flux_json_request_decode (*zmsg, &o) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    if (!Jget_int (o, "count", &count)) {
        if (flux_err_respond (h, EPROTO, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    for (i = 0; i < count; i++) {
        zmsg_t *cpy = zmsg_dup (*zmsg);
        if (!cpy)
            oom ();
        Jadd_int (o, "seq", i);
        if (flux_json_respond (h, o, &cpy) < 0)
            flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                      strerror (errno));
        zmsg_destroy (&cpy);
    }
    zmsg_destroy (zmsg);
done:
    Jput (o);
    return 0;
}

/* Always return an error 42
 */
static int err_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    if (flux_err_respond (h, 42, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
    return 0;
}

/* Echo a json payload back to requestor.
 */
static int echo_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = NULL;

    if (flux_json_request_decode (*zmsg, &o) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    if (flux_json_respond (h, o, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
done:
    Jput (o);
    return 0;
}

/* Proxy ping.
 */
static int xping_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    int rank, seq = ctx->ping_seq++;
    const char *service;
    char *hashkey = NULL;
    JSON in = Jnew ();
    JSON o = NULL;

    if (flux_json_request_decode (*zmsg, &o) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    if (!Jget_int (o, "rank", &rank) || !Jget_str (o, "service", &service)) {
        if (flux_err_respond (h, EPROTO, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    flux_log (h, LOG_DEBUG, "Rxping rank=%d service=%s", rank, service);

    Jadd_int (in, "seq", seq);
    flux_log (h, LOG_DEBUG, "Tping seq=%d %d!%s", seq, rank, service);
    if (flux_json_request (h, rank, 0, service, in) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                      strerror (errno));
        goto done;
    }
    hashkey = xasprintf ("%d", seq);
    zhash_update (ctx->ping_requests, hashkey, *zmsg);
    *zmsg = NULL;
done:
    Jput (o);
    Jput (in);
    if (hashkey)
        free (hashkey);
    return 0;
}

/* Handle ping response for proxy ping.
 */
static int ping_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON o = NULL;
    JSON out = Jnew ();;
    int seq;
    const char *route;
    zmsg_t *zreq = NULL;
    char *hashkey = NULL;

    if (flux_json_response_decode (*zmsg, &o) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_json_response_decode: %s",
                  __FUNCTION__, strerror (errno));
        goto done;
    }
    if (!Jget_int (o, "seq", &seq) || !Jget_str (o, "route", &route)) {
        flux_log (h, LOG_ERR, "%s: protocol error", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "Rping seq=%d %s", seq, route);
    hashkey = xasprintf ("%d", seq);
    if (!(zreq = zhash_lookup (ctx->ping_requests, hashkey))) {
        flux_log (h, LOG_ERR, "%s: unsolicited ping response: %s",
                  __FUNCTION__, hashkey);
        goto done;
    }
    zhash_delete (ctx->ping_requests, hashkey);
    flux_log (h, LOG_DEBUG, "Txping seq=%d %s", seq, route);
    Jadd_str (out, "route", route);
    if (flux_json_respond (h, out, &zreq) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_json_respond: %s",
                  __FUNCTION__, strerror (errno));
        goto done;
    }
done:
    if (hashkey)
        free (hashkey);
    Jput (o);
    Jput (out);
    //zmsg_destroy (zmsg);
    zmsg_destroy (&zreq);
    return 0;
}

/* Handle the simplest possible request.
 * Verify that everything is as expected; log it and stop the reactor if not.
 */
static int null_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    //ctx_t *ctx = arg;
    char *topic = NULL;
    int type, size, flags;
    int rc = -1;
    void *buf;
    uint32_t nodeid;

    if (!zmsg || !*zmsg) {
        flux_log (h, LOG_ERR, "%s: got NULL zmsg!", __FUNCTION__);
        goto done;
    }
    if (flux_msg_get_type (*zmsg, &type) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_msg_get_type: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (type != FLUX_MSGTYPE_REQUEST) {
        flux_log (h, LOG_ERR, "%s: unexpected type %s", __FUNCTION__,
                  flux_msgtype_string (type));
        goto done;
    }
    if (flux_msg_get_nodeid (*zmsg, &nodeid, &flags) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_msg_get_nodeid: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (nodeid != FLUX_NODEID_ANY && nodeid != flux_rank (h)) {
        flux_log (h, LOG_ERR, "%s: unexpected nodeid: %"PRIu32"", __FUNCTION__,
                  nodeid);
        goto done;
    }
    if (flux_msg_get_topic (*zmsg, &topic) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_msg_get_topic: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (strcmp (topic, "req.null") != 0) {
        flux_log (h, LOG_ERR, "%s: unexpected topic: %s", __FUNCTION__,
                  topic);
        goto done;
    }
    if (flux_msg_get_payload (*zmsg, &flags, &buf, &size) == 0) {
        flux_log (h, LOG_ERR, "%s: unexpected payload size %d", __FUNCTION__,
                  size);
        goto done;
    }
    if (errno != EPROTO) {
        flux_log (h, LOG_ERR, "%s: get nonexistent payload: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    errno = 0;
    if (flux_err_respond (h, 0, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST, "req.null",              null_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.echo",              echo_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.err",               err_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.src",               src_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.nsrc",              nsrc_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.sink",              sink_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.xping",             xping_request_cb },
    { FLUX_MSGTYPE_RESPONSE, "req.ping",             ping_response_cb },
    { FLUX_MSGTYPE_REQUEST, "req.clog",              clog_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.flush",             flush_request_cb },
    { FLUX_MSGTYPE_REQUEST, "req.count",             count_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("req");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
