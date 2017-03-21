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
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>
#include <pthread.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

void test_null (flux_t *h, uint32_t nodeid);
void test_echo (flux_t *h, uint32_t nodeid);
void test_err (flux_t *h, uint32_t nodeid);
void test_src (flux_t *h, uint32_t nodeid);
void test_sink (flux_t *h, uint32_t nodeid);
void test_nsrc (flux_t *h, uint32_t nodeid);
void test_putmsg (flux_t *h, uint32_t nodeid);
void test_pingzero (flux_t *h, uint32_t nodeid);
void test_pingself (flux_t *h, uint32_t nodeid);
void test_pingupstream (flux_t *h, uint32_t nodeid);
void test_flush (flux_t *h, uint32_t nodeid);
void test_clog (flux_t *h, uint32_t nodeid);
void test_coproc (flux_t *h, uint32_t nodeid);

typedef struct {
    const char *name;
    void (*fun)(flux_t *h, uint32_t nodeid);
} test_t;

static test_t tests[] = {
    { "null",   &test_null },
    { "echo",   &test_echo },
    { "err",    &test_err },
    { "src",    &test_src },
    { "sink",   &test_sink },
    { "nsrc",   &test_nsrc },
    { "putmsg", &test_putmsg },
    { "pingzero", &test_pingzero},
    { "pingself", &test_pingself},
    { "pingupstream", &test_pingupstream},
    { "flush", &test_flush},
    { "clog", &test_clog},
    { "coproc", &test_coproc},
};

test_t *test_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (tests) / sizeof (tests[0]); i++)
        if (!strcmp (tests[i].name, name))
            return &tests[i];
    return NULL;
}

#define OPTIONS "hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: treq [--rank N] {null | echo | err | src | sink | nsrc | putmsg | pingzero | pingself | pingupstream | clog | flush | coproc}\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    uint32_t nodeid = FLUX_NODEID_ANY;
    test_t *t;

    log_init ("treq");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    if (!(t = test_lookup (argv[optind])))
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    t->fun (h, nodeid);

    flux_close (h);

    log_fini ();
    return 0;
}

void test_null (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;

    if (!(rpc = flux_rpc (h, "req.null", NULL, nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("req.null");
    flux_rpc_destroy (rpc);
}

void test_echo (flux_t *h, uint32_t nodeid)
{
    json_object *in = Jnew ();
    json_object *out = NULL;
    const char *json_str;
    const char *s;
    flux_rpc_t *rpc;

    Jadd_str (in, "mumble", "burble");
    if (!(rpc = flux_rpc (h, "req.echo", Jtostr (in), nodeid, 0))
             || flux_rpc_get (rpc, &json_str) < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (!json_str
        || !(out = Jfromstr (json_str))
        || !Jget_str (out, "mumble", &s)
        || strcmp (s, "burble") != 0)
        log_msg_exit ("%s: returned payload wasn't an echo", __FUNCTION__);
    Jput (in);
    Jput (out);
    flux_rpc_destroy (rpc);
}

void test_err (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;

    if (!(rpc = flux_rpc (h, "req.err", NULL, nodeid, 0)))
        log_err_exit ("error sending request");
    if (flux_rpc_get (rpc, NULL) == 0)
        log_msg_exit ("%s: succeeded when should've failed", __FUNCTION__);
    if (errno != 42)
        log_msg_exit ("%s: got errno %d instead of 42", __FUNCTION__, errno);
    flux_rpc_destroy (rpc);
}

void test_src (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    const char *json_str;
    json_object *out = NULL;
    int i;

    if (!(rpc = flux_rpc (h, "req.src", NULL, nodeid, 0))
             || flux_rpc_get (rpc, &json_str) < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (!json_str
        || !(out = Jfromstr (json_str))
        || !Jget_int (out, "wormz", &i)
        || i != 42)
        log_msg_exit ("%s: didn't get expected payload", __FUNCTION__);
    Jput (out);
    flux_rpc_destroy (rpc);
}

void test_sink (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    json_object *in = Jnew();

    Jadd_double (in, "pi", 3.14);
    if (!(rpc = flux_rpc (h, "req.sink", Jtostr (in), nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("%s", __FUNCTION__);
    Jput (in);
    flux_rpc_destroy (rpc);
}

void test_nsrc (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    const int count = 10000;
    json_object *in = Jnew ();
    const char *json_str;
    json_object *out = NULL;
    int i, seq;

    Jadd_int (in, "count", count);
    if (!(rpc = flux_rpc (h, "req.nsrc", Jtostr (in), FLUX_NODEID_ANY,
                                                      FLUX_RPC_NORESPONSE)))
        log_err_exit ("%s", __FUNCTION__);

    for (i = 0; i < count; i++) {
        flux_msg_t *msg = flux_recv (h, FLUX_MATCH_ANY, 0);
        if (!msg)
            log_err_exit ("%s", __FUNCTION__);
        if (flux_response_decode (msg, NULL, &json_str) < 0)
            log_msg_exit ("%s: decode %d", __FUNCTION__, i);
        if (!json_str
            || !(out = Jfromstr (json_str))
            || !Jget_int (out, "seq", &seq))
            log_msg_exit ("%s: decode %d payload", __FUNCTION__, i);
        if (seq != i)
            log_msg_exit ("%s: decode %d - seq mismatch %d", __FUNCTION__, i, seq);
        Jput (out);
        flux_msg_destroy (msg);
    }
    Jput (in);
}

/* This test is to make sure that deferred responses are handled in order.
 * Arrange for module to source 10K sequenced responses.  Messages 5000-5499
 * are "put back" on the handle using flux_putmsg().  We ensure that
 * the 10K messages are nonetheless received in order.
 */
void test_putmsg (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    const char *json_str;
    const int count = 10000;
    const int defer_start = 5000;
    const int defer_count = 500;
    json_object *in = Jnew ();
    json_object *out = NULL;
    int seq, myseq = 0;
    zlist_t *defer = zlist_new ();
    bool popped = false;
    flux_msg_t *z;

    if (!defer)
        oom ();

    Jadd_int (in, "count", count);
    if (!(rpc = flux_rpc (h, "req.nsrc", Jtostr (in), FLUX_NODEID_ANY,
                                                      FLUX_RPC_NORESPONSE)))
        log_err_exit ("%s", __FUNCTION__);
    flux_rpc_destroy (rpc);
    do {
        flux_msg_t *msg = flux_recv (h, FLUX_MATCH_ANY, 0);
        if (!msg)
            log_err_exit ("%s", __FUNCTION__);
        if (flux_response_decode (msg, NULL, &json_str) < 0)
            log_msg_exit ("%s: decode", __FUNCTION__);
        if (!json_str
            || !(out = Jfromstr (json_str))
            || !Jget_int (out, "seq", &seq))
            log_msg_exit ("%s: decode - payload", __FUNCTION__);
        Jput (out);
        if (seq >= defer_start && seq < defer_start + defer_count && !popped) {
            if (zlist_append (defer, msg) < 0)
                oom ();
            if (seq == defer_start + defer_count - 1) {
                while ((z = zlist_pop (defer))) {
                    if (flux_requeue (h, z, FLUX_RQ_TAIL) < 0)
                        log_err_exit ("%s: flux_requeue", __FUNCTION__);
                    flux_msg_destroy (z);
                }
                popped = true;
            }
            continue;
        }
        if (seq != myseq)
            log_msg_exit ("%s: expected %d got %d", __FUNCTION__, myseq, seq);
        myseq++;
        flux_msg_destroy (msg);
    } while (myseq < count);
    zlist_destroy (&defer);
    Jput (in);
}

static int count_hops (const char *s)
{
    const char *p = s;
    int count = 0;
    if (strlen (p) > 0)
        count++;
    while ((p = strchr (p, '!'))) {
        p++;
        count++;
    }
    return count;
}

static void xping (flux_t *h, uint32_t nodeid, uint32_t xnodeid, const char *svc)
{
    flux_rpc_t *rpc;
    const char *json_str;
    json_object *in = Jnew ();
    json_object *out = NULL;
    const char *route;

    Jadd_int (in, "rank", xnodeid);
    Jadd_str (in, "service", svc);
    if (!(rpc = flux_rpc (h, "req.xping", Jtostr (in), nodeid, 0))
            || flux_rpc_get (rpc, &json_str) < 0)
        log_err_exit ("req.xping");
    if (!json_str
        || !(out = Jfromstr (json_str))
        || !Jget_str (out, "route", &route))
        log_errn_exit (EPROTO, "req.xping");
    printf ("hops=%d\n", count_hops (route));
    Jput (out);
    Jput (in);
    flux_rpc_destroy (rpc);
}

void test_pingzero (flux_t *h, uint32_t nodeid)
{
    xping (h, nodeid, 0, "req.ping");
}

void test_pingupstream (flux_t *h, uint32_t nodeid)
{
    xping (h, nodeid, FLUX_NODEID_UPSTREAM, "req.ping");
}

void test_pingself (flux_t *h, uint32_t nodeid)
{
    xping (h, nodeid, nodeid, "req.ping");
}

void test_flush (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    if (!(rpc = flux_rpc (h, "req.flush", NULL, nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("req.flush");
    flux_rpc_destroy (rpc);
}

void test_clog (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    if (!(rpc = flux_rpc (h, "req.clog", NULL, nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("req.clog");
    flux_rpc_destroy (rpc);
}

/* Coprocess test: requires 'req' and 'coproc' modules loaded
 * - aux thread: coproc.stuck RPC which hangs internally
 *     (coproc reactor should continue though due to COPROC flag)
 * - main thread: verify coproc.stuck sent req.clog request
 * - main thread: "ping" coproc.hi (must respond!)
 * - main thread: allow clog response via req.flush
 */

void *thd (void *arg)
{
    flux_rpc_t *rpc;
    uint32_t *nodeid = arg;
    flux_t *h;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(rpc = flux_rpc (h, "coproc.stuck", NULL, *nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("coproc.stuck");

    flux_rpc_destroy (rpc);
    flux_close (h);
    return NULL;
}

int req_count (flux_t *h, uint32_t nodeid)
{
    flux_rpc_t *rpc;
    const char *json_str;
    json_object *out = NULL;
    int rc = -1;
    int count;

    if (!(rpc = flux_rpc (h, "req.count", NULL, nodeid, 0))
             || flux_rpc_get (rpc, &json_str) < 0)
        goto done;
    if (!json_str
        || !(out = Jfromstr (json_str))
        || !Jget_int (out, "count", &count)) {
        errno = EPROTO;
        goto done;
    }
    rc = count;
done:
    flux_rpc_destroy (rpc);
    Jput (out);
    return rc;
}

void test_coproc (flux_t *h, uint32_t nodeid)
{
    pthread_t t;
    int rc;
    int count, count0;
    flux_rpc_t *rpc;

    if ((count0 = req_count (h, nodeid)) < 0)
        log_err_exit ("req.count");

    if ((rc = pthread_create (&t, NULL, thd, &nodeid)))
        log_errn_exit (rc, "pthread_create");

    do {
        //usleep (100*1000); /* 100ms */
        if ((count = req_count (h, nodeid)) < 0)
            log_err_exit ("req.count");
    } while (count - count0 < 1);
    log_msg ("%d requests are stuck", count - count0);

    if (!(rpc = flux_rpc (h, "coproc.hi", NULL, nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("coproc.hi");
    flux_rpc_destroy (rpc);
    log_msg ("hi request was answered");

    if (!(rpc = flux_rpc (h, "req.flush", NULL, nodeid, 0))
             || flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("req.flush");
    flux_rpc_destroy (rpc);
    if ((count = req_count (h, nodeid)) < 0)
        log_err_exit ("req.count");
    if (count != 0)
        log_msg_exit ("request was not flushed");

    if ((rc = pthread_join (t, NULL)))
        log_errn_exit (rc, "pthread_join");
    log_msg ("thread finished");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
