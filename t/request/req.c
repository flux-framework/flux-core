#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    flux_t *h;
    zhash_t *ping_requests;
    int ping_seq;
    zlist_t *clog_requests;
    uint32_t rank;
} t_req_ctx_t;

static void freectx (void *arg)
{
    t_req_ctx_t *ctx = arg;
    flux_msg_t *msg;

    if (ctx) {
        zhash_destroy (&ctx->ping_requests);
        if (ctx->clog_requests) {
            while ((msg = zlist_pop (ctx->clog_requests)))
                flux_msg_destroy (msg);
            zlist_destroy (&ctx->clog_requests);
        }
        free (ctx);
    }
}

static t_req_ctx_t *getctx (flux_t *h)
{
    int saved_errno;
    t_req_ctx_t *ctx = (t_req_ctx_t *)flux_aux_get (h, "req");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->ping_requests = zhash_new ();
        ctx->clog_requests = zlist_new ();
        if (!ctx->clog_requests || !ctx->ping_requests) {
            saved_errno = ENOMEM;
            goto error; 
        }
        if (flux_get_rank (h, &ctx->rank) < 0) {
            saved_errno = errno;
            goto error;
        }
        flux_aux_set (h, "req", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    errno = saved_errno;
    return NULL;
}

/* Return number of queued clog requests
 */
void count_request_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = getctx (h);
    json_object *o = Jnew ();

    Jadd_int (o, "count", zlist_size (ctx->clog_requests));
    if (flux_respond (h, msg, 0, Jtostr (o)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (o);
}

/* Don't reply to request - just queue it for later.
 */
void clog_request_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = getctx (h);
    flux_msg_t *cpy = flux_msg_copy (msg, true);

    if (zlist_push (ctx->clog_requests, cpy) < 0)
        oom ();
}

/* Reply to all queued requests.
 */
void flush_request_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = getctx (h);
    flux_msg_t *req;

    while ((req = zlist_pop (ctx->clog_requests))) {
        /* send clog response */
        if (flux_respond (h, req, 0, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    /* send flush response */
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

/* Accept a json payload, verify it and return error if it doesn't
 * match expected.
 */
void sink_request_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    int saved_errno;
    json_object *o = NULL;
    double d;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_double (o, "pi", &d)
        || d != 3.14) {
        saved_errno = errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (o);
}

/* Return a fixed json payload
 */
void src_request_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    json_object *o = Jnew ();

    Jadd_int (o, "wormz", 42);
    if (flux_respond (h, msg, 0, Jtostr (o)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (o);
}

/* Return 'n' sequenced responses.
 */
void nsrc_request_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    int saved_errno;
    json_object *o = Jnew ();
    int i, count;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_int (o, "count", &count)) {
        saved_errno = errno = EPROTO;
        goto done;
    }
    for (i = 0; i < count; i++) {
        Jadd_int (o, "seq", i);
        if (flux_respond (h, msg, 0, Jtostr (o)) < 0) {
            saved_errno = errno;
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
            goto done;
        }
    }
    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond (h, msg, saved_errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    Jput (o);
}

/* Always return an error 42
 */
void err_request_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    if (flux_respond (h, msg, 42, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

/* Echo a json payload back to requestor.
 */
void echo_request_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    int saved_errno;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (!json_str) {
        saved_errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              rc < 0 ? NULL : json_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

/* Proxy ping.
 */
void xping_request_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = arg;
    const char *json_str;
    int saved_errno;
    int rank, seq = ctx->ping_seq++;
    const char *service;
    char *hashkey = NULL;
    json_object *in = Jnew ();
    json_object *o = NULL;
    flux_msg_t *cpy;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = errno;
        goto error;
    }
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_int (o, "rank", &rank)
        || !Jget_str (o, "service", &service)) {
        saved_errno = errno = EPROTO;
        goto error;
    }
    flux_log (h, LOG_DEBUG, "Rxping rank=%d service=%s", rank, service);

    Jadd_int (in, "seq", seq);
    flux_log (h, LOG_DEBUG, "Tping seq=%d %d!%s", seq, rank, service);

    flux_future_t *f;
    if (!(f = flux_rpc (h, service, Jtostr (in), rank,
                                            FLUX_RPC_NORESPONSE))) {
        saved_errno = errno;
        goto error;
    }
    flux_future_destroy (f);
    if (!(cpy = flux_msg_copy (msg, true))) {
        saved_errno = errno;
        goto error;
    }
    hashkey = xasprintf ("%d", seq);
    zhash_update (ctx->ping_requests, hashkey, cpy);
    zhash_freefn (ctx->ping_requests, hashkey, (zhash_free_fn *)flux_msg_destroy);
    Jput (o);
    Jput (in);
    if (hashkey)
        free (hashkey);
    return;
error:
    if (flux_respond (h, msg, saved_errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (o);
    Jput (in);
}

/* Handle ping response for proxy ping.
 * Match it with a request and respond to that request.
 */
void ping_response_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = arg;
    const char *json_str;
    json_object *o = NULL;
    json_object *out = Jnew ();;
    int seq;
    const char *route;
    flux_msg_t *req = NULL;
    char *hashkey = NULL;

    if (flux_response_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (h, "%s: flux_response_decode", __FUNCTION__);
        goto done;
    }
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_int (o, "seq", &seq)
        || !Jget_str (o, "route", &route)) {
        errno = EPROTO;
        flux_log_error (h, "%s: payload", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "Rping seq=%d %s", seq, route);
    hashkey = xasprintf ("%d", seq);
    if (!(req = zhash_lookup (ctx->ping_requests, hashkey))) {
        flux_log_error (h, "%s: unsolicited ping response", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "Txping seq=%d %s", seq, route);
    Jadd_str (out, "route", route);
    if (flux_respond (h, req, 0, Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    zhash_delete (ctx->ping_requests, hashkey);
done:
    if (hashkey)
        free (hashkey);
    Jput (o);
    Jput (out);
}

/* Handle the simplest possible request.
 * Verify that everything is as expected; log it and stop the reactor if not.
 */
void null_request_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    t_req_ctx_t *ctx = arg;
    const char *topic;
    int type, size;
    const void *buf;
    uint32_t nodeid;
    int flags;

    if (!msg) {
        flux_log (h, LOG_ERR, "%s: got NULL msg!", __FUNCTION__);
        goto error;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        flux_log_error (h, "%s: flux_msg_get_type", __FUNCTION__);
        goto error;
    }
    if (type != FLUX_MSGTYPE_REQUEST) {
        flux_log (h, LOG_ERR, "%s: unexpected type %s", __FUNCTION__,
                  flux_msg_typestr (type));
        goto error;
    }
    if (flux_msg_get_nodeid (msg, &nodeid, &flags) < 0) {
        flux_log_error (h, "%s: flux_msg_get_nodeid", __FUNCTION__);
        goto error;
    }
    if (nodeid != ctx->rank && nodeid != FLUX_NODEID_ANY) {
        flux_log (h, LOG_ERR, "%s: unexpected nodeid: %"PRIu32"", __FUNCTION__,
                  nodeid);
        goto error;
    }
    if (flux_msg_get_topic (msg, &topic) < 0) {
        flux_log_error (h, "%s: flux_msg_get_topic", __FUNCTION__);
        goto error;
    }
    if (strcmp (topic, "req.null") != 0) {
        flux_log (h, LOG_ERR, "%s: unexpected topic: %s", __FUNCTION__,
                  topic);
        goto error;
    }
    if (flux_msg_get_payload (msg, &flags, &buf, &size) == 0) {
        flux_log (h, LOG_ERR, "%s: unexpected payload size %d", __FUNCTION__,
                  size);
        goto error;
    }
    if (errno != EPROTO) {
        flux_log (h, LOG_ERR, "%s: get nonexistent payload: %s", __FUNCTION__,
                  strerror (errno));
        goto error;
    }
    if (flux_respond (h, msg, 0, NULL) < 0) {
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
        goto error;
    }
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (h));
}

struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "req.null",              null_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.echo",              echo_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.err",               err_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.src",               src_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.nsrc",              nsrc_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.sink",              sink_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.xping",             xping_request_cb, 0, NULL },
    { FLUX_MSGTYPE_RESPONSE, "req.ping",             ping_response_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.clog",              clog_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.flush",             flush_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "req.count",             count_request_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int saved_errno;
    t_req_ctx_t *ctx = getctx (h);

    if (!ctx) {
        saved_errno = errno;
        flux_log_error (h, "error allocating context");
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_msg_handler_addvec");
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_reactor_run");
        flux_msg_handler_delvec (htab);
        goto error;
    }
    flux_msg_handler_delvec (htab);
    return 0;
error:
    errno = saved_errno;
    return -1;
}

MOD_NAME ("req");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
