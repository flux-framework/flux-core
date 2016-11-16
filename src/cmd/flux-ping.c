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
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

struct ping_ctx {
    double interval;    /* interval between sends, in seconds */
    char *rank;         /* target rank(s) if multiple or NULL */
    uint32_t nodeid;    /* target nodeid if single */
    char *topic;        /* target topic string */
    char *pad;          /* pad string */
    int count;          /* number of pings to send */
    int send_count;     /* sending count */
    bool batch;         /* begin receiving only after count sent */
    flux_t *h;
    flux_reactor_t *reactor;
};

struct ping_data {
    tstat_t *tstat;
    int seq;
    char *route;
    unsigned int rpc_count;
};

#define OPTIONS "hp:i:r:c:b"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"pad",        required_argument,  0, 'p'},
    {"interval",   required_argument,  0, 'i'},
    {"count",      required_argument,  0, 'c'},
    {"batch",      no_argument,        0, 'b'},
    { 0, 0, 0, 0 },
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

void usage (void)
{
    fprintf (stderr,
"Usage: flux-ping [--rank NODESET] [--pad bytes] [--interval sec] [--count N] [--batch] target\n"
);
    exit (1);
}

/* Handle responses
 */
void ping_continuation (flux_rpc_t *rpc, void *arg)
{
    struct ping_ctx *ctx = arg;
    const char *json_str, *route, *pad;
    int64_t sec, nsec;
    struct timespec t0;
    int seq;
    json_object *out = NULL;
    struct ping_data *pdata = flux_rpc_aux_get (rpc);
    tstat_t *tstat = pdata->tstat;

    if (flux_rpc_get (rpc, &json_str) < 0) {
        log_err ("flux_rpc_get");
        goto done;
    }
    if (!(out = Jfromstr (json_str))
            || !Jget_int (out, "seq", &seq)
            || !Jget_int64 (out, "time.tv_sec", &sec)
            || !Jget_int64 (out, "time.tv_nsec", &nsec)
            || !Jget_str (out, "pad", &pad)
            || !Jget_str (out, "route", &route)
            || strcmp (ctx->pad, pad) != 0) {
        log_err ("error decoding ping response");
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
            if (ctx->rank != NULL) {
                printf ("%s!%s pad=%lu seq=%d time=(%0.3f:%0.3f:%0.3f) ms "
                        "stddev %0.3f\n",
                        ctx->rank, ctx->topic, strlen (ctx->pad), pdata->seq,
                        tstat_min (tstat), tstat_mean (tstat),
                        tstat_max (tstat), tstat_stddev (tstat));
            } else {
                char s[16];
                snprintf (s, sizeof (s), "%u", ctx->nodeid);
                printf ("%s%s%s pad=%lu seq=%d time=%0.3f ms (%s)\n",
                        ctx->nodeid == FLUX_NODEID_ANY ? "" : s,
                        ctx->nodeid == FLUX_NODEID_ANY ? "" : "!",
                        ctx->topic, strlen (ctx->pad), pdata->seq,
                        tstat_mean (tstat), pdata->route);
            }
        }
        flux_rpc_destroy (rpc);
    }
    Jput (out);
}

void send_ping (struct ping_ctx *ctx)
{
    struct timespec t0;
    json_object *in = Jnew ();
    flux_rpc_t *rpc;
    struct ping_data *pdata = xzmalloc (sizeof (*pdata));

    pdata->tstat = xzmalloc (sizeof (*(pdata->tstat)));
    pdata->seq = 0;
    pdata->route = NULL;
    pdata->rpc_count = 0;

    Jadd_int (in, "seq", ctx->send_count);
    monotime (&t0);
    Jadd_int64 (in, "time.tv_sec", t0.tv_sec);
    Jadd_int64 (in, "time.tv_nsec", t0.tv_nsec);
    Jadd_str (in, "pad", ctx->pad);

    if (ctx->rank)
        rpc = flux_rpc_multi (ctx->h, ctx->topic, Jtostr (in), ctx->rank, 0);
    else
        rpc = flux_rpc (ctx->h, ctx->topic, Jtostr (in), ctx->nodeid, 0);
    if (!rpc)
        log_err_exit ("flux_rpc");
    flux_rpc_aux_set (rpc, pdata, ping_data_free);
    if (flux_rpc_then (rpc, ping_continuation, ctx) < 0)
        log_err_exit ("flux_rpc_then");

    Jput (in);

    ctx->send_count++;
}

/* Send a request each time the timer fires.
 * After 'ctx->count' requests have been sent, stop the watcher.
 */
void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct ping_ctx *ctx = arg;

    send_ping (ctx);
    if (ctx->send_count == ctx->count)
        flux_watcher_stop (w);
    else if (ctx->interval == 0.) { /* needs rearm if repeat is 0. */
        flux_timer_watcher_reset (w, ctx->interval, ctx->interval);
        flux_watcher_start (w);
    }
}

int main (int argc, char *argv[])
{
    int ch;
    int pad_bytes = 0;
    char *target;
    flux_watcher_t *tw = NULL;
    struct ping_ctx ctx = {
        .interval = 1.0,
        .rank = NULL,
        .nodeid = FLUX_NODEID_ANY,
        .topic = NULL,
        .pad = NULL,
        .count = -1,
        .send_count = 0,
        .batch = false,
    };

    log_init ("flux-ping");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pad bytes */
                pad_bytes = strtoul (optarg, NULL, 10);
                break;
            case 'i': /* --interval seconds */
                ctx.interval = strtod (optarg, NULL);
                if (ctx.interval < 0)
                    usage ();
                break;
            case 'r': /* --rank NODESET  */
                ctx.rank = optarg;
                break;
            case 'c': /* --count N */
                ctx.count = strtoul (optarg, NULL, 10);
                break;
            case 'b': /* --batch-request */
                ctx.batch = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    if (ctx.batch && ctx.count == -1)
        log_msg_exit ("--batch should only be used with --count");
    target = argv[optind++];

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
        nodeset_t *ns = NULL;
        if ((p = strchr (target, '!'))) {
            *p++ = '\0';
            ctx.rank = target;
            target = p;
        } else if ((ns = nodeset_create_string (target)) != NULL) {
            ctx.rank = target;
            target = "cmb";
            nodeset_destroy (ns);
        } else if (!strcmp (target, "all")) {
            ctx.rank = target;
            target = "cmb";
        }
    }
    /* Use singleton rpc if there's only one nodeid
     */
    if (ctx.rank != NULL) {
        nodeset_t *ns = nodeset_create_string (ctx.rank);
        if (ns) {
            if (nodeset_count (ns) == 1) {
                ctx.nodeid = nodeset_min (ns);
                ctx.rank = NULL;
            }
            nodeset_destroy (ns);
        }
    }

    ctx.topic = xasprintf ("%s.ping", target);

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(ctx.reactor = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

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
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
