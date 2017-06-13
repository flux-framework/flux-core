#include <stdio.h>
#include <inttypes.h>
#include <flux/core.h>
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/xzmalloc.h"

struct context {
    int batchnum;
    flux_reduce_t *r;
    char rankstr[16];
    flux_t *h;
};

int itemweight (void *item)
{
    int count = 1;
    nodeset_t *nodeset;

    if ((nodeset = nodeset_create_string (item))) {
        count = nodeset_count (nodeset);
        nodeset_destroy (nodeset);
    }
    return count;
}

void sink (flux_reduce_t *r, int batchnum, void *arg)
{
    char *item;

    while ((item = flux_reduce_pop (r))) {
        fprintf (stderr, "%d: %s\n", batchnum, item);
        free (item);
    }
}

void forward (flux_reduce_t *r, int batchnum, void *arg)
{
    struct context *ctx = arg;
    char *item;
    flux_future_t *f;

    while ((item = flux_reduce_pop (r))) {
        json_object *out = Jnew ();
        Jadd_int (out, "batchnum", batchnum);
        Jadd_str (out, "nodeset", item);
        f = flux_rpc (ctx->h, "treduce.forward", Jtostr (out),
                        FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE);
        flux_future_destroy (f);
        Jput (out);
        free (item);
    }
}

void reduce (flux_reduce_t *r, int batchnum, void *arg)
{
    nodeset_t *nodeset = NULL;
    char *item;

    if ((item = flux_reduce_pop (r))) {
        nodeset = nodeset_create_string (item);
        free (item);
    }
    if (nodeset) {
        while ((item = flux_reduce_pop (r))) {
            nodeset_add_string (nodeset, item);
            free (item);
        }
        item = xstrdup (nodeset_string (nodeset));
        if (flux_reduce_push (r, item) < 0)
            free (item);
        nodeset_destroy (nodeset);
    }
}

void forward_cb (flux_t *h, flux_msg_handler_t *w,
                 const flux_msg_t *msg, void *arg)
{
    struct context *ctx = arg;
    const char *json_str, *nodeset_str;
    json_object *in = NULL;
    int batchnum;
    char *item;

    if (flux_request_decode (msg, NULL, &json_str) < 0
            || !json_str
            || !(in = Jfromstr (json_str))
            || !Jget_int (in, "batchnum", &batchnum)
            || !Jget_str (in, "nodeset", &nodeset_str))
        return;
    item = xstrdup (nodeset_str);
    if (flux_reduce_append (ctx->r, item, batchnum) < 0)
        free (item);
    Jput (in);
}

void heartbeat_cb (flux_t *h, flux_msg_handler_t *w,
                   const flux_msg_t *msg, void *arg)
{
    struct context *ctx = arg;
    char *item = xstrdup (ctx->rankstr);
    if (flux_reduce_append (ctx->r, item, ctx->batchnum++) < 0)
        free (item);
}

struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     "hb",              heartbeat_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,   "treduce.forward", forward_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

struct flux_reduce_ops reduce_ops = {
    .destroy = free,
    .itemweight = itemweight,
    .reduce = reduce,
    .forward = forward,
    .sink = sink,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    struct context ctx;
    uint32_t rank;
    double timeout = 0.;
    int flags;

    if (argc == 1) {
        timeout = strtod (argv[0], NULL);
        flags = FLUX_REDUCE_TIMEDFLUSH;
    } else
        flags = FLUX_REDUCE_HWMFLUSH;
    ctx.batchnum = 0;
    if (flux_get_rank (h, &rank) < 0)
        return -1;
    snprintf (ctx.rankstr, sizeof (ctx.rankstr), "%"PRIu32, rank);
    ctx.h = h;

    if (!(ctx.r = flux_reduce_create (h, reduce_ops, timeout, &ctx, flags)))
        return -1;
    if (flux_event_subscribe (h, "hb") < 0)
        return -1;
    if (flux_msg_handler_addvec (h, htab, &ctx) < 0)
        return -1;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        return -1;
    flux_msg_handler_delvec (htab);
    return 0;
}

MOD_NAME ("treduce");
