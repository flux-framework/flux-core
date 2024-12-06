/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rpcscale.c - send a batch of requests / handle batch of responses */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/optparse.h>
#include <sys/resource.h>

#include "src/common/libutil/monotime.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libtestutil/util.h"

#include "tap.h"

void ping_cb (flux_t *h,
              flux_msg_handler_t *mh,
              const flux_msg_t *msg,
              void *arg)
{
    const void *payload;
    size_t payload_len;

    if (flux_request_decode_raw (msg, NULL, &payload, &payload_len) < 0)
        goto error;
    if (flux_msg_is_noresponse (msg))
        return;
    if (flux_respond_raw (h, msg, payload, payload_len) < 0)
        diag ("error responding to ping: %s", strerror (errno));
    // if --streaming, terminate the RPC with ENODATA response
    if (flux_msg_is_streaming (msg)) {
        errno = ENODATA;
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("error responding to ping: %s", strerror (errno));
}

const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "ping",       ping_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int test_server (flux_t *h, void *arg)
{
    flux_msg_handler_t **handlers = NULL;
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        diag ("flux_msg_handler_addvec failed");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    flux_msg_handler_delvec (handlers);
    return 0;
}

static int response_count;

void ping_continuation (flux_future_t *f, void *arg)
{
    optparse_t *p = arg;
    bool streaming = optparse_hasopt (p, "streaming");

    response_count++;

    if (flux_rpc_get (f, NULL) < 0) {
        if (errno != ENODATA)
            diag ("ping error: %s", strerror (errno));
        flux_future_destroy (f);
        return;
    }
    if (streaming)
        flux_future_reset (f);
    else
        flux_future_destroy (f);
}

void logger (const char *buf, int len, void *arg)
{
    diag ("%.*s", len, buf);
}

static struct optparse_option opts[] = {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Set message count per iteration (default 10000)",
    },
    { .name = "iter", .key = 'I', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of iterations (default 2)",
    },
    { .name = "rpctrack", .key = 'r', .has_arg = 0,
      .usage = "Enable FLUX_O_RPCTRACK",
    },
    { .name = "matchdebug", .key = 'd', .has_arg = 0,
      .usage = "Enable FLUX_O_MATCHDEBUG",
    },
    { .name = "streaming", .key = 's', .has_arg = 0,
      .usage = "Enable FLUX_RPC_STREAMING",
    },
    { .name = "noresponse", .key = 'n', .has_arg = 0,
      .usage = "Enable FLUX_RPC_NORESPONSE",
    },
    { .name = "pad", .key = 'p', .has_arg = 1, .arginfo = "N[kKMGPE]",
      .usage = "pad message with payload",
    },
    OPTPARSE_TABLE_END
};

int main (int argc, char *argv[])
{
    optparse_t *p;
    flux_t *h;
    int test_size;
    int test_iterations;
    struct rusage ru;
    int open_flags = 0;
    int rpc_flags = 0;
    uint64_t payload_size = 0;
    void *payload = NULL;

    plan (NO_PLAN);

    if (!(p = optparse_create ("rpcscale")))
        BAIL_OUT ("optparse_create");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        BAIL_OUT ("optparse_add_option_table() failed");
    if (optparse_parse_args (p, argc, argv) < argc)
        BAIL_OUT ("Type rpcscale -h for options.");
    test_size = optparse_get_int (p, "count", 10000);
    test_iterations = optparse_get_int (p, "iter", 2);
    if (optparse_hasopt (p, "rpctrack"))
        open_flags |= FLUX_O_RPCTRACK;
    if (optparse_hasopt (p, "matchdebug"))
        open_flags |= FLUX_O_MATCHDEBUG;
    if (optparse_hasopt (p, "streaming"))
        rpc_flags |= FLUX_RPC_STREAMING;
    if (optparse_hasopt (p, "noresponse"))
        rpc_flags |= FLUX_RPC_NORESPONSE;
    if (optparse_hasopt (p, "pad")) {
        const char *s = optparse_get_str (p, "pad", "0");
        if (parse_size (s, &payload_size) < 0)
            BAIL_OUT ("could not parse pad size");
        if (!(payload = calloc (1, payload_size)))
            BAIL_OUT ("out of memory");
    }
    h = test_server_create (open_flags, test_server, NULL);
    ok (h != NULL,
        "created test server thread");
    if (!h)
        BAIL_OUT ("can't continue without test server");
    flux_log_set_redirect (h, logger, NULL);

    for (int iter = 1; iter <= test_iterations; iter++) {
        int errors;
        int rc;
        struct timespec t0;
        double t;
        flux_future_t *f;

        diag ("Iteration %d of %d", iter, test_iterations);

        /* Send
         */
        monotime (&t0);
        errors = 0;
        for (int i = 0; i < test_size; i++) {
            if (!(f = flux_rpc_raw (h,
                                    "ping",
                                    payload,
                                    payload_size,
                                    FLUX_NODEID_ANY,
                                    rpc_flags))) {
                diag ("error sending rpc #%d", i);
                errors++;
            }
            else if ((rpc_flags & FLUX_RPC_NORESPONSE)) {
                flux_future_destroy (f);
            }
            else {
                if (flux_future_then (f, -1, ping_continuation, p) < 0) {
                    diag ("error registeirng continuation for rpc #%d", i);
                    flux_future_destroy (f);
                }
            }
        }
        t = monotime_since (t0) / 1000;
        if (getrusage (RUSAGE_SELF, &ru) < 0)
            BAIL_OUT ("gerusage failed");
        diag ("send %d req in %.2fs (%.1f Kmsg/s) rss %.1fMB",
              test_size,
              t,
              1E-3 * test_size / t,
              1E-3 * ru.ru_maxrss);
        ok (errors == 0,
            "sent batch of requests with no errors");

        /* Recv
         */
        response_count = 0;
        monotime (&t0);
        rc = flux_reactor_run (flux_get_reactor (h), 0);
        t = monotime_since (t0) / 1000;
        if (getrusage (RUSAGE_SELF, &ru) < 0)
            BAIL_OUT ("gerusage failed");
        diag ("recv %d rep in %.2fs (%.1f Kmsg/s) rss %.1fMB",
              response_count,
              t,
              1E-3 * response_count / t,
              1E-3 * ru.ru_maxrss);
        ok (rc == 0,
            "processed responses with no errors");
        if (rc != 0)
            diag ("reactor returned %d", rc);

        /* Wait for server to clear requests if --noresponse
         */
        if ((rpc_flags & FLUX_RPC_NORESPONSE)) {
            flux_future_t *f2;
            if (!(f2 = flux_rpc (h, "ping", NULL, FLUX_NODEID_ANY, 0))
                || flux_rpc_get (f2, NULL) < 0)
                BAIL_OUT ("synchronous ping failed");
            flux_future_destroy (f2);
        }
    }

    ok (test_server_stop (h) == 0,
        "stopped test server thread");
    flux_close (h); // destroys test server

    free (payload);
    optparse_destroy (p);

    done_testing();
    return (0);
}

// vi:ts=4 sw=4 expandtab
