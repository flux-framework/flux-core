#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    flux_t h;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "req");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        flux_aux_set (h, "req", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
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
    if (flux_msg_get_nodeid (*zmsg, &nodeid) < 0) {
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
