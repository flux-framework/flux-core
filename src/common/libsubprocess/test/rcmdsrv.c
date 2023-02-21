/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <unistd.h> // environ def
#include <signal.h>
#include <jansson.h>
#include <flux/core.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libsubprocess/server.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/stdlog.h"

struct rcmdsrv {
    flux_msg_handler_t **handlers;
};

/* This allows broker log requests from the libsubprocess code to
 * appear in test output.
 */
static void log_cb (flux_t *h,
                    flux_msg_handler_t *mh,
                    const flux_msg_t *msg,
                    void *arg)
{
    const char *buf;
    int len;
    struct stdlog_header hdr;
    int textlen;
    const char *text;

    if (flux_request_decode_raw (msg, NULL, (const void **)&buf, &len) == 0
        && stdlog_decode (buf, len, &hdr, NULL, NULL, &text, &textlen) == 0)
        diag ("LOG: %.*s\n", textlen, text);
    else
        diag ("LOG: could not decode message\n");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "log.append", log_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static int test_server (flux_t *h, void *arg)
{
    int rc = -1;
    subprocess_server_t *srv = NULL;
    flux_msg_handler_t **handlers = NULL;

    if (flux_attr_set_cacheonly (h, "rank", "0") < 0) {
        diag ("flux_attr_set_cacheonly");
        goto done;
    }
    if (!(srv = subprocess_server_create (h, "smurf", 0))) {
        diag ("subprocess_server_create failed");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, srv, &handlers) < 0) {
        diag ("error registering message handler");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        goto done;
    }
    diag ("destroying subprocess server");
    subprocess_server_destroy (srv);
    diag ("server reactor exiting");
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    return rc;
}

// called on flux_t handle destroy
static void rcmdsrv_destroy (struct rcmdsrv *ctx)
{
    if (ctx) {
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
    };
}

flux_t *rcmdsrv_create (void)
{
    flux_t *h;
    struct rcmdsrv *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        BAIL_OUT ("out of memory");

    // Without this, process may be terminated by SIGPIPE when writing
    // to stdin of subprocess that has terminated
    signal (SIGPIPE, SIG_IGN);

    // N.B. test reactor is created with FLUX_REACTOR_SIGCHLD flag
    if (!(h = test_server_create (0, test_server, NULL)))
        BAIL_OUT ("test_server_create failed");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly failed");
    // register log handle on this end for server-side logging
    if (flux_msg_handler_addvec (h, htab, NULL, &ctx->handlers) < 0)
        BAIL_OUT ("error registering message handlers");
    flux_reactor_active_decref (flux_get_reactor (h)); // don't count logger
    if (flux_aux_set (h, "testserver", ctx, (flux_free_f)rcmdsrv_destroy) < 0)
        BAIL_OUT ("error storing server context in aux container");
    return h;
};

// vi: ts=4 sw=4 expandtab
