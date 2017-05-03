/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

struct ping_ctx {
    double interval;    /* interval between sends, in seconds */
    const char *rank;   /* target rank(s) if multiple or NULL */
    uint32_t rank_count;/* number of ranks in rank */
    char *topic;        /* target topic string */
    char *pad;          /* pad string */
    int count;          /* number of pings to send */
    int send_count;     /* sending count */
    bool batch;         /* begin receiving only after count sent */
    flux_t *h;
    flux_reactor_t *reactor;
    bool userid;        /* include userid/rolemask in output */
};

struct ping_data {
    tstat_t *tstat;
    int seq;
    char *route;
    unsigned int rpc_count;
};

static struct optparse_option cmdopts[] = {
    { .name = "rank",     .key = 'r', .has_arg = 1, .arginfo = "NODESET",
      .usage = "Find target on a specific broker rank(s)",
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
        if (pdata->tstat)
            free (pdata->tstat);
        if (pdata->route)
            free (pdata->route);
        free (pdata);
    }
}

/* Handle responses
 */
void ping_continuation (flux_rpc_t *rpc, void *arg)
{
    struct ping_ctx *ctx = arg;
    const char *route, *pad;
    int64_t sec, nsec;
    struct timespec t0;
    int seq;
    struct ping_data *pdata = flux_rpc_aux_get (rpc, "ping");
    tstat_t *tstat = pdata->tstat;
    uint32_t rolemask, userid;

    if (flux_rpc_getf (rpc, "{ s:i s:I s:I s:s s:s s:i s:i !}",
                       "seq", &seq,
                       "time.tv_sec", &sec,
                       "time.tv_nsec", &nsec,
                       "pad", &pad,
                       "route", &route,
                       "userid", &userid,
                       "rolemask", &rolemask) < 0) {
        log_err ("%s!%s", ctx->rank, ctx->topic);
        goto done;
    }

    if (strcmp (ctx->pad, pad) != 0) {
        log_err ("error in ping pad");
        goto done;
    }

    t0.tv_sec = sec;
    t0.tv_nsec = nsec;
    tstat_push (tstat, monotime_since (t0));

    pdata->seq = seq;
    if (pdata->route)
        free (pdata->route);
    pdata->route = xstrdup (route);
    pdata->rpc_count++;

done:
    if (flux_rpc_next (rpc) < 0) {
        if (pdata->rpc_count) {
            if (ctx->rank_count > 1) {
                printf ("%s!%s pad=%zu seq=%d time=(%0.3f:%0.3f:%0.3f) ms "
                        "stddev %0.3f\n",
                        ctx->rank, ctx->topic, strlen (ctx->pad), pdata->seq,
                        tstat_min (tstat), tstat_mean (tstat),
                        tstat_max (tstat), tstat_stddev (tstat));
            } else {
                char s[16] = {0};
                char u[32] = {0};
                if (strcmp (ctx->rank, "any"))
                    snprintf (s, sizeof (s), "%s!", ctx->rank);
                if (ctx->userid)
                    snprintf (u, sizeof (u),
                              " userid=%" PRIu32 " rolemask=0x%" PRIx32,
                              userid, rolemask);
                printf ("%s%s pad=%zu%s seq=%d time=%0.3f ms (%s)\n",
                        s, ctx->topic, strlen (ctx->pad), u, pdata->seq,
                        tstat_mean (tstat), pdata->route);
            }
        }
        flux_rpc_destroy (rpc);
    }
}

void send_ping (struct ping_ctx *ctx)
{
    struct timespec t0;
    flux_rpc_t *rpc;
    struct ping_data *pdata = xzmalloc (sizeof (*pdata));

    pdata->tstat = xzmalloc (sizeof (*(pdata->tstat)));
    pdata->seq = 0;
    pdata->route = NULL;
    pdata->rpc_count = 0;

    monotime (&t0);

    rpc = flux_rpcf_multi (ctx->h, ctx->topic, ctx->rank, 0,
                           "{s:i s:I s:I s:s}",
                           "seq", ctx->send_count,
                           "time.tv_sec", (uint64_t)t0.tv_sec,
                           "time.tv_nsec", (uint64_t)t0.tv_nsec,
                           "pad", ctx->pad);
    if (!rpc)
        log_err_exit ("flux_rpcf_multi");
    if (flux_rpc_aux_set (rpc, "ping", pdata, ping_data_free) < 0)
        log_err_exit ("flux_rpc_aux_set");
    if (flux_rpc_then (rpc, ping_continuation, ctx) < 0)
        log_err_exit ("flux_rpc_then");

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

int main (int argc, char *argv[])
{
    int pad_bytes;
    char *target;
    flux_watcher_t *tw = NULL;
    nodeset_t *ns = NULL;
    optparse_t *opts;
    struct ping_ctx ctx;
    int optindex;

    memset (&ctx, 0, sizeof (ctx));

    log_init ("flux-ping");

    opts = optparse_create ("flux-ping");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    pad_bytes = optparse_get_int (opts, "pad", 0);
    if (pad_bytes < 0)
        log_msg_exit ("pad must be >= 0");

    ctx.rank = optparse_get_str (opts, "rank", NULL);

    ctx.interval = optparse_get_double (opts, "interval", 1.0);
    if (ctx.interval < 0.)
        log_msg_exit ("interval must be >= 0");

    ctx.count = optparse_get_int (opts, "count", 0);
    if (ctx.count < 0)
        log_msg_exit ("count must be >= 0");

    ctx.batch = optparse_hasopt (opts, "batch");
    ctx.userid = optparse_hasopt (opts, "userid");

    if (ctx.batch && ctx.count == 0)
        log_msg_exit ("--batch should only be used with --count");

    if (optindex != argc - 1) {
        optparse_print_usage (opts);
        exit (1);
    }

    target = argv[optindex++];

    /* Create null terminated pad string for reuse in each message.
     * By default it's the empty string.
     */
    ctx.pad = xzmalloc (pad_bytes + 1);
    memset (ctx.pad, 'p', pad_bytes);

    /* If "rank!" is prepended to the target, and there is no --rank
     * argument, snip it off and set the rank.  If it's just the bare
     * rank, assume the target is "cmb".
     */
    if (ctx.rank == NULL) {
        char *p;
        if ((p = strchr (target, '!'))) {
            *p++ = '\0';
            ctx.rank = target;
            target = p;
        } else if ((ns = nodeset_create_string (target)) != NULL) {
            ctx.rank = target;
            target = "cmb";
            nodeset_destroy (ns);
        } else if (!strcmp (target, "all")
                   || !strcmp (target, "any")
                   || !strcmp (target, "upstream")) {
            ctx.rank = target;
            target = "cmb";
        }
        else
            ctx.rank = "any";
    }

    ctx.topic = xasprintf ("%s.ping", target);

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(ctx.reactor = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

    /* Determine number of ranks for output logic
     */
    if ((ns = nodeset_create_string (ctx.rank))) {
        ctx.rank_count = nodeset_count (ns);
        nodeset_destroy (ns);
    }
    else {
        if (!strcmp (ctx.rank, "all")) {
            if (flux_get_size (ctx.h, &ctx.rank_count) < 0)
                log_err_exit ("flux_get_size");
        }
        else
            ctx.rank_count = 1;
    }

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

    flux_close (ctx.h);
    optparse_destroy (opts);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
