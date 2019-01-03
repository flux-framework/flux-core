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
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>
#include <pthread.h>
#include <flux/core.h>
#include <czmq.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"

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
"Usage: treq [--rank N] {null | echo | err | src | sink | nsrc | putmsg | pingzero | pingself | pingupstream | clog | flush}\n"
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
    flux_future_t *f;

    if (!(f = flux_rpc (h, "req.null", NULL, nodeid, 0))
             || flux_future_get (f, NULL) < 0)
        log_err_exit ("req.null");
    flux_future_destroy (f);
}

void test_echo (flux_t *h, uint32_t nodeid)
{
    const char *s;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h, "req.echo", nodeid, 0,
                             "{s:s}", "mumble", "burble"))
             || flux_rpc_get_unpack (f, "{s:s}", "mumble", &s) < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (strcmp (s, "burble") != 0)
        log_msg_exit ("%s: returned payload wasn't an echo", __FUNCTION__);
    flux_future_destroy (f);
}

void test_err (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h, "req.err", NULL, nodeid, 0)))
        log_err_exit ("error sending request");
    if (flux_future_get (f, NULL) == 0)
        log_msg_exit ("%s: succeeded when should've failed", __FUNCTION__);
    if (errno != 42)
        log_msg_exit ("%s: got errno %d instead of 42", __FUNCTION__, errno);
    flux_future_destroy (f);
}

void test_src (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;
    int i;

    if (!(f = flux_rpc (h, "req.src", NULL, nodeid, 0))
             || flux_rpc_get_unpack (f, "{s:i}", "wormz", &i) < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (i != 42)
        log_msg_exit ("%s: didn't get expected payload", __FUNCTION__);
    flux_future_destroy (f);
}

void test_sink (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h, "req.sink", nodeid, 0, "{s:f}", "pi", 3.14))
             || flux_future_get (f, NULL) < 0)
        log_err_exit ("%s", __FUNCTION__);
    flux_future_destroy (f);
}

void test_nsrc (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;
    const char *json_str;
    const int count = 10000;
    int i, seq = -1;
    json_t *o;

    if (!(f = flux_rpc_pack (h, "req.nsrc",
                             FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE,
                             "{s:i}", "count", count)))
        log_err_exit ("%s", __FUNCTION__);
    flux_future_destroy (f);
    for (i = 0; i < count; i++) {
        flux_msg_t *msg;
        if (!(msg = flux_recv (h, FLUX_MATCH_ANY, 0)))
            log_err_exit ("%s", __FUNCTION__);
        if (flux_response_decode (msg, NULL, &json_str) < 0)
            log_msg_exit ("%s: decode %d", __FUNCTION__, i);
        if (!json_str || !(o = json_loads (json_str, 0, NULL))
                      || json_unpack (o, "{s:i}", "seq", &seq) < 0)
            log_msg_exit ("%s: decode %d payload", __FUNCTION__, i);
        if (seq != i)
            log_msg_exit ("%s: decode %d - seq mismatch %d", __FUNCTION__, i, seq);
        json_decref (o);
        flux_msg_destroy (msg);
    }
}

/* This test is to make sure that deferred responses are handled in order.
 * Arrange for module to source 10K sequenced responses.  Messages 5000-5499
 * are "put back" on the handle using flux_putmsg().  We ensure that
 * the 10K messages are nonetheless received in order.
 */
void test_putmsg (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;
    const char *json_str;
    const int count = 10000;
    const int defer_start = 5000;
    const int defer_count = 500;
    int seq, myseq = 0;
    zlist_t *defer = zlist_new ();
    bool popped = false;
    flux_msg_t *z;
    json_t *o;

    if (!defer)
        oom ();

    if (!(f = flux_rpc_pack (h, "req.nsrc",
                             FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE,
                             "{s:i}", "count", count)))
        log_err_exit ("%s", __FUNCTION__);
    flux_future_destroy (f);
    do {
        flux_msg_t *msg = flux_recv (h, FLUX_MATCH_ANY, 0);
        if (!msg)
            log_err_exit ("%s", __FUNCTION__);
        if (flux_response_decode (msg, NULL, &json_str) < 0)
            log_msg_exit ("%s: decode", __FUNCTION__);
        if (!json_str || !(o = json_loads (json_str, 0, NULL))
                      || json_unpack (o, "{s:i}", "seq", &seq) < 0)
            log_msg_exit ("%s: decode - payload", __FUNCTION__);
        json_decref (o);
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
    flux_future_t *f;
    const char *route;

    if (!(f = flux_rpc_pack (h, "req.xping", nodeid, 0,
                             "{s:i s:s}", "rank", xnodeid, "service", svc))
            || flux_rpc_get_unpack (f, "{s:s}", "route", &route) < 0)
        log_err_exit ("req.xping");
    printf ("hops=%d\n", count_hops (route));
    flux_future_destroy (f);
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
    flux_future_t *f;
    if (!(f = flux_rpc (h, "req.flush", NULL, nodeid, 0))
             || flux_future_get (f, NULL) < 0)
        log_err_exit ("req.flush");
    flux_future_destroy (f);
}

void test_clog (flux_t *h, uint32_t nodeid)
{
    flux_future_t *f;
    if (!(f = flux_rpc (h, "req.clog", NULL, nodeid, 0))
             || flux_rpc_get (f, NULL) < 0)
        log_err_exit ("req.clog");
    flux_future_destroy (f);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
