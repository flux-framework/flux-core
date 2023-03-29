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
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

struct ping_ctx {
    double interval;    /* interval between sends, in seconds */
    uint32_t nodeid;    /* target rank or FLUX_NODEID_ANY */
    char *topic;        /* target topic string */
    char *pad;          /* pad string */
    int count;          /* number of pings to send */
    int send_count;     /* sending count */
    bool batch;         /* begin receiving only after count sent */
    flux_t *h;
    flux_reactor_t *reactor;
    bool userid_flag;   /* include userid/rolemask in output */
};

struct ping_data {
    tstat_t *tstat;
    int seq;
    char *route;
    unsigned int rpc_count;
};

static struct optparse_option cmdopts[] = {
    { .name = "rank",     .key = 'r', .has_arg = 1, .arginfo = "RANK",
      .usage = "Find target on a specific broker rank",
    },
    { .name = "pad",      .key = 'p', .has_arg = 1, .arginfo = "N",
      .usage = "Include in the payload a string of length N bytes",
    },
    { .name = "interval", .key = 'i', .has_arg = 1, .arginfo = "N",
      .usage = "Specify the delay, in seconds, between successive requests",
    },
    { .name = "count",    .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Specify the number of requests to send",
    },
    { .name = "batch",    .key = 'b', .has_arg = 0,
      .usage = "Begin processing responses after all requests are sent",
    },
    { .name = "userid",   .key = 'u', .has_arg = 0,
      .usage = "Include userid and rolemask in ping output",
    },
    OPTPARSE_TABLE_END
};

void ping_data_free (void *ctx)
{
    struct ping_data *pdata = ctx;
    if (pdata) {
        free (pdata->tstat);
        free (pdata->route);
        free (pdata);
    }
}

char *cred_str (uint32_t userid, uint32_t rolemask, char *buf, int bufsz)
{
    if (snprintf (buf,
                  bufsz,
                  " userid=%" PRIu32 " rolemask=0x%" PRIx32,
                  userid,
                  rolemask) >= bufsz)
        return "";
    return buf;
}

char *rank_bang_str (uint32_t rank, char *buf, int bufsz)
{
    switch (rank) {
    case FLUX_NODEID_ANY:
        return "";
    case FLUX_NODEID_UPSTREAM:
        return "upstream!";
    default:
        if (snprintf (buf, bufsz, "%" PRIu32 "!", rank) >= bufsz)
            return "";
        return buf;
    }
}

/* Handle responses
 */
void ping_continuation (flux_future_t *f, void *arg)
{
    struct ping_ctx *ctx = arg;
    const char *route, *pad;
    int64_t sec, nsec;
    struct timespec t0;
    int seq;
    struct ping_data *pdata = flux_future_aux_get (f, "ping");
    tstat_t *tstat = pdata->tstat;
    uint32_t rolemask, userid, rank;
    char ubuf[32];
    char rbuf[32];

    if (flux_rpc_get_unpack (f,
                             "{ s:i s:I s:I s:s s:s s:i s:i s:i !}",
                             "seq",
                             &seq,
                             "time.tv_sec",
                             &sec,
                             "time.tv_nsec",
                             &nsec,
                             "pad",
                             &pad,
                             "route",
                             &route,
                             "userid",
                             &userid,
                             "rolemask",
                             &rolemask,
                             "rank",
                             &rank) < 0)
        log_err_exit ("%s%s",
                      rank_bang_str (ctx->nodeid, rbuf, sizeof (rbuf)),
                      ctx->topic);

    if (strcmp (ctx->pad, pad) != 0)
        log_msg_exit ("%s%s: padding contents invalid",
                      rank_bang_str (ctx->nodeid, rbuf, sizeof (rbuf)),
                      ctx->topic);


    t0.tv_sec = sec;
    t0.tv_nsec = nsec;
    tstat_push (tstat, monotime_since (t0));

    pdata->seq = seq;
    if (pdata->route)
        free (pdata->route);
    pdata->route = xstrdup (route);
    pdata->rpc_count++;

    printf ("%s%s pad=%zu%s seq=%d time=%0.3f ms (%s)\n",
            rank_bang_str (rank, rbuf, sizeof (rbuf)),
            ctx->topic,
            strlen (ctx->pad),
            ctx->userid_flag
                ? cred_str (userid, rolemask, ubuf, sizeof (ubuf)) : "",
            pdata->seq,
            tstat_mean (tstat),
            pdata->route);

    flux_future_destroy (f);
}

void send_ping (struct ping_ctx *ctx)
{
    struct timespec t0;
    flux_future_t *f;
    struct ping_data *pdata = xzmalloc (sizeof (*pdata));

    pdata->tstat = xzmalloc (sizeof (*(pdata->tstat)));
    pdata->seq = 0;
    pdata->route = NULL;
    pdata->rpc_count = 0;

    monotime (&t0);

    if (!(f = flux_rpc_pack (ctx->h,
                             ctx->topic, ctx->nodeid,
                             0,
                             "{s:i s:I s:I s:s}",
                             "seq",
                             ctx->send_count,
                             "time.tv_sec",
                             (uint64_t)t0.tv_sec,
                             "time.tv_nsec",
                             (uint64_t)t0.tv_nsec,
                             "pad",
                             ctx->pad)))
        log_err_exit ("flux_rpc_pack");
    if (flux_future_aux_set (f, "ping", pdata, ping_data_free) < 0)
        log_err_exit ("flux_future_aux_set");
    if (flux_future_then (f, -1., ping_continuation, ctx) < 0)
        log_err_exit ("flux_future_then");

    ctx->send_count++;
}

/* Send a request each time the timer fires.
 * After 'ctx->count' requests have been sent, stop the watcher.
 */
void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct ping_ctx *ctx = arg;

    send_ping (ctx);
    if (ctx->count && ctx->send_count == ctx->count)
        flux_watcher_stop (w);
    else if (ctx->interval == 0.) { /* needs rearm if repeat is 0. */
        flux_timer_watcher_reset (w, ctx->interval, ctx->interval);
        flux_watcher_start (w);
    }
}

int parse_nodeid (struct ping_ctx *ctx,
                  const char *input,
                  uint32_t *nodeid,
                  char **nodeidstr,
                  char **suffixstr)
{
    int rank;
    char *endptr;

    if (!strcmp (input, "any")) {
        *nodeid = FLUX_NODEID_ANY;
        (*nodeidstr) = xasprintf ("%s", "any");
        return 0;
    }
    if (!strcmp (input, "upstream")) {
        *nodeid = FLUX_NODEID_UPSTREAM;
        (*nodeidstr) = xasprintf ("%s", "upstream");
        return 0;
    }
    if ((rank = flux_get_rankbyhost (ctx->h, input)) >= 0) {
        *nodeid = rank;
        (*nodeidstr) = xasprintf ("%s", input);
        (*suffixstr) = xasprintf (" (rank %d)", rank);
        return 0;
    }
    errno = 0;
    rank = strtol (input, &endptr, 10);
    if (errno == 0 && *endptr == '\0' && rank >= 0) {
        *nodeid = rank;
        (*nodeidstr) = xasprintf ("%d", rank);
        return 0;
    }
    return -1;
}

void parse_target (struct ping_ctx *ctx, const char *target, char **header)
{
    char *cpy;
    char *service;
    char *nodeidstr = NULL;
    char *suffixstr = NULL;

    if (!(cpy = strdup (target)))
        log_err_exit ("out of memory");

    /* TARGET specifies nodeid!service */
    if ((service = strchr (cpy, '!'))) {
        *service++ = '\0';
        if (parse_nodeid (ctx, cpy, &ctx->nodeid, &nodeidstr, &suffixstr) < 0)
            log_msg_exit ("invalid nodeid/host: '%s'", cpy);
    }
    /* TARGET only specifies service, assume nodeid is ANY */
    else if (parse_nodeid (ctx, cpy, &ctx->nodeid, &nodeidstr, &suffixstr) < 0) {
        service = cpy;
        ctx->nodeid = FLUX_NODEID_ANY;
        nodeidstr = xasprintf ("%s", "any");
    }
    /* TARGET only specifies nodeid, assume service is "broker" */
    else
        service = "broker";
    ctx->topic = xasprintf ("%s.ping", service);
    (*header) = xasprintf ("flux-ping %s!%s%s",
                           nodeidstr,
                           service,
                           suffixstr ? suffixstr : "");
    free (cpy);
    free (nodeidstr);
    free (suffixstr);
}

int main (int argc, char *argv[])
{
    int pad_bytes;
    char *cmdusage = "[OPTIONS] TARGET";
    flux_watcher_t *tw = NULL;
    optparse_t *opts;
    struct ping_ctx ctx;
    int optindex;
    char *target;
    char *header = NULL;

    memset (&ctx, 0, sizeof (ctx));

    log_init ("flux-ping");

    opts = optparse_create ("flux-ping");
    if (optparse_set (opts, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);
    if (optindex != argc - 1) {
        optparse_print_usage (opts);
        exit (1);
    }
    if (!(target = strdup (argv[optindex])))
        log_msg_exit ("out of memory");

    pad_bytes = optparse_get_int (opts, "pad", 0);
    if (pad_bytes < 0)
        log_msg_exit ("pad must be >= 0");

    ctx.nodeid = FLUX_NODEID_ANY;
    if (optparse_hasopt (opts, "rank")) {
        const char *s = optparse_get_str (opts, "rank", NULL);
        if (!s)
            log_msg_exit ("error parsing --rank option");
        if (strchr (target, '!'))
            log_msg_exit ("--rank and TARGET both try to specify a nodeid");
        char *new_target = xasprintf ("%s!%s", s, target);
        free (target);
        target = new_target;
    }

    ctx.interval = optparse_get_duration (opts, "interval", 1.0);
    if (ctx.interval < 0.)
        log_msg_exit ("interval must be >= 0");

    ctx.count = optparse_get_int (opts, "count", 0);
    if (ctx.count < 0)
        log_msg_exit ("count must be >= 0");

    ctx.batch = optparse_hasopt (opts, "batch");
    ctx.userid_flag = optparse_hasopt (opts, "userid");

    if (ctx.batch && ctx.count == 0)
        log_msg_exit ("--batch should only be used with --count");

    /* Create null terminated pad string for reuse in each message.
     * By default it's the empty string.
     */
    ctx.pad = xzmalloc (pad_bytes + 1);
    memset (ctx.pad, 'p', pad_bytes);

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(ctx.reactor = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

    /* Set ctx.nodeid and ctx.topic from TARGET argument
     */
    parse_target (&ctx, target, &header);

    printf ("%s\n", header);
    /* In batch mode, requests are sent before reactor is started
     * to process responses.  o/w requests are set in a timer watcher.
     */
    if (ctx.batch) {
        while (ctx.send_count < ctx.count) {
            send_ping (&ctx);
            usleep ((useconds_t)(ctx.interval * 1E6));
        }
    } else {
        tw = flux_timer_watcher_create (ctx.reactor, 0, ctx.interval,
                                        timer_cb, &ctx);
        if (!tw)
            log_err_exit ("error creating watchers");
        flux_watcher_start (tw);
    }
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    /* Clean up.
     */
    flux_watcher_destroy (tw);

    free (ctx.topic);
    free (ctx.pad);

    free (target);
    free (header);
    flux_close (ctx.h);
    optparse_destroy (opts);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
