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
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"

struct ping_ctx {
    double period;      /* interval between sends, in seconds */
    uint32_t nodeid;    /* target nodeid (or FLUX_NODEID_ANY) */
    char *topic;        /* target topic string */
    char *pad;          /* pad string */
    int count;          /* number of pings to send */
    int send_count;     /* sending count */
    bool batch;         /* begin receiving only after count sent */
    flux_t h;
    flux_reactor_t *reactor;
};

#define OPTIONS "hp:d:r:c:b"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"pad",        required_argument,  0, 'p'},
    {"delay",      required_argument,  0, 'd'},
    {"count",      required_argument,  0, 'c'},
    {"batch",      no_argument,        0, 'b'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-ping [--rank N] [--pad bytes] [--delay sec] [--count N] [--batch] target\n"
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
    JSON out = NULL;
    char rankprefix[16];
    uint32_t nodeid;

    if (flux_rpc_get (rpc, &nodeid, &json_str) < 0) {
        err ("flux_rpc_get");
        goto done;
    }
    if (!(out = Jfromstr (json_str))
            || !Jget_int (out, "seq", &seq)
            || !Jget_int64 (out, "time.tv_sec", &sec)
            || !Jget_int64 (out, "time.tv_nsec", &nsec)
            || !Jget_str (out, "pad", &pad)
            || !Jget_str (out, "route", &route)
            || strcmp (ctx->pad, pad) != 0) {
        err ("error decoding ping response");
        goto done;
    }
    t0.tv_sec = sec;
    t0.tv_nsec = nsec;

    snprintf (rankprefix, sizeof (rankprefix), "%u!", nodeid);
    printf ("%s%s pad=%lu seq=%d time=%0.3f ms (%s)\n",
                nodeid == FLUX_NODEID_ANY ? "" : rankprefix, ctx->topic,
                strlen (ctx->pad), seq, monotime_since (t0), route);
done:
    Jput (out);
    if (flux_rpc_completed (rpc))
        flux_rpc_destroy (rpc);
}

void send_ping (struct ping_ctx *ctx)
{
    struct timespec t0;
    JSON in = Jnew ();
    flux_rpc_t *rpc;

    Jadd_int (in, "seq", ctx->send_count);
    monotime (&t0);
    Jadd_int64 (in, "time.tv_sec", t0.tv_sec);
    Jadd_int64 (in, "time.tv_nsec", t0.tv_nsec);
    Jadd_str (in, "pad", ctx->pad);

    if (!(rpc = flux_rpc (ctx->h, ctx->topic, Jtostr (in), ctx->nodeid, 0)))
        err_exit ("flux_rpc");
    if (flux_rpc_then (rpc, ping_continuation, ctx) < 0)
        err_exit ("flux_rpc_then");

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
    else if (ctx->period == 0.) { /* needs rearm if repeat is 0. */
        flux_timer_watcher_reset (w, ctx->period, ctx->period);
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
        .period = 1.0,
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
            case 'd': /* --delay seconds */
                ctx.period = strtod (optarg, NULL);
                if (ctx.period < 0)
                    usage ();
                break;
            case 'r': /* --rank N */
                ctx.nodeid = strtoul (optarg, NULL, 10);
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
        msg_exit ("--batch should only be used with --count");
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
    if (ctx.nodeid == FLUX_NODEID_ANY) {
        char *endptr;
        uint32_t n = strtoul (target, &endptr, 10);
        if (endptr != target)
            ctx.nodeid = n;
        if (*endptr == '!')
            target = endptr + 1;
        else
            target = endptr;
    }
    if (*target == '\0')
        target = "cmb";
    ctx.topic = xasprintf ("%s.ping", target);

    if (!(ctx.h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (!(ctx.reactor = flux_get_reactor (ctx.h)))
        err_exit ("flux_get_reactor");

    /* In batch mode, requests are sent before reactor is started
     * to process responses.  o/w requests are set in a timer watcher.
     */
    if (ctx.batch) {
        while (ctx.send_count < ctx.count) {
            send_ping (&ctx);
            usleep ((useconds_t)(ctx.period * 1E6));
        }
    } else {
        tw = flux_timer_watcher_create (ctx.reactor, ctx.period, ctx.period,
                                        timer_cb, &ctx);
        if (!tw)
            err_exit ("error creating watchers");
        flux_watcher_start (tw);
    }
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        err_exit ("flux_reactor_run");

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
